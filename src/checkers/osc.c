#include <osc/parser.h>
#include <osc/debug.h>
#include <osc/check_list.h>
#include <osc/check_ownership.h>
#include <osc/arena.h>
#include <osc/context.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <stdio.h>

/**
 * osc_attr_init - Initialize function parameter ownership lattice states
 * @ctx: Active compilation context
 * @fn: Target CFG function
 * @state: Initial dataflow state for entry basic block
 */
static void osc_attr_init(struct osc_context *ctx, struct cfg_function *fn,
                          struct dataflow_state *state)
{
    struct ast_decl *param = NULL;

    if (!ctx || !fn || !state)
        return;

    if (fn->decl && fn->decl->type && fn->decl->type->is_function) {
        list_for_each_entry (param, &fn->decl->type->params, node) {
            if (param->type && (param->type->osc_attrs & ATTR_FLAGS_MUT)) {
                dataflow_state_set_var(state, param, VAR_STATE_SET,
                                       param->span);
            } else if (param->type &&
                       (param->type->osc_attrs & ATTR_FLAGS_BRW)) {
                dataflow_state_set_var(state, param, VAR_STATE_BORROWED,
                                       param->span);
            }
        }
    }
}

/**
 * osc_attr_transfer - Transfer function evaluating CFG 3AC instructions
 * @ctx: Active compilation context
 * @insn: Instruction being evaluated
 * @state: Mutable block dataflow state
 */
static void osc_attr_transfer(struct osc_context *ctx, struct instruction *insn,
                              struct dataflow_state *state)
{
    struct ast_decl *decl = NULL;
    struct ast_decl *callee_decl = NULL;
    struct ast_decl *param = NULL;
    struct cfg_arg_node *arg = NULL;
    struct dataflow_var_state *v = NULL;
    struct dataflow_var_state *v_src = NULL;
    size_t arg_idx = 0;
    bool is_mut_param = false;

    if (!ctx || !insn || !state)
        return;

    switch (insn->opcode) {
    case OP_ALLOC:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            if (decl->type &&
                (decl->type->osc_attrs & (ATTR_FLAGS_MUT | ATTR_FLAGS_BRW))) {
                dataflow_state_set_var(state, decl, VAR_STATE_UNTRACKED,
                                       insn->span);
            }
        }
        break;

    case OP_STORE:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (insn->is_deref) {
                /* Dereference store: *decl = ... */
                if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_BRW) &&
                    !(decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                    osc_report_diagnostic(
                        ctx, insn->span, "Don't write to the borrowed object",
                        NULL, (struct osc_span){ 0 },
                        insn->bb ? insn->bb->func : NULL, decl);
                }
                if (v && v->state == VAR_STATE_DROPPED) {
                    osc_report_diagnostic(
                        ctx, insn->span, "Don't write to the dropped object",
                        "Dropped at", v->drop_span,
                        insn->bb ? insn->bb->func : NULL, decl);
                }
            } else {
                /* Pointer or value assignment: decl = ... */
                if (insn->src1.kind == CFG_OPR_CONST &&
                    insn->src1.const_val == 0) {
                    /* Disarming / clearing pointer: decl = NULL */
                    if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_MUT))
                        dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                               insn->span);
                } else if (insn->src1.kind == CFG_OPR_VAR &&
                           insn->src1.var_decl) {
                    /* Moving ownership from src to decl */
                    v_src = dataflow_state_get_var(state, insn->src1.var_decl);
                    if (v_src && v_src->state == VAR_STATE_DROPPED) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the dropped object", "Dropped at",
                            v_src->drop_span, insn->bb ? insn->bb->func : NULL,
                            insn->src1.var_decl);
                    }
                    if (insn->src1.var_decl->type &&
                        (insn->src1.var_decl->type->osc_attrs &
                         ATTR_FLAGS_MUT)) {
                        dataflow_state_set_var(state, insn->src1.var_decl,
                                               VAR_STATE_DROPPED, insn->span);
                    }
                    if (decl->type &&
                        (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                               insn->span);
                    } else if (decl->type &&
                               (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                        dataflow_state_set_var(state, decl, VAR_STATE_BORROWED,
                                               insn->span);
                    }
                } else {
                    /* Initializing / setting decl */
                    if (decl->type &&
                        (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                               insn->span);
                    } else if (decl->type &&
                               (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                        dataflow_state_set_var(state, decl, VAR_STATE_BORROWED,
                                               insn->span);
                    }
                }
            }
        }
        break;

    case OP_CALL:
        callee_decl = osc_find_decl(ctx, insn->callee_sym);
        arg_idx = 0;

        list_for_each_entry (arg, &insn->args, node) {
            if (arg->op.kind == CFG_OPR_VAR && arg->op.var_decl) {
                decl = arg->op.var_decl;
                v = dataflow_state_get_var(state, decl);
                is_mut_param = false;

                if (callee_decl && callee_decl->type &&
                    callee_decl->type->is_function) {
                    size_t p_idx = 0;
                    list_for_each_entry (param, &callee_decl->type->params,
                                         node) {
                        if (p_idx == arg_idx) {
                            if (param->type &&
                                (param->type->osc_attrs & ATTR_FLAGS_MUT))
                                is_mut_param = true;
                            break;
                        }
                        p_idx++;
                    }
                } else if (insn->callee_sym) {
                    const char *cname = insn->callee_sym->name;
                    if (!strcmp(cname, "release") || !strcmp(cname, "free") ||
                        !strcmp(cname, "kfree") || !strncmp(cname, "put_", 4) ||
                        !strncmp(cname, "drop_", 5)) {
                        is_mut_param = true;
                    } else if (decl->type &&
                               (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        is_mut_param = true;
                    }
                } else if (decl->type &&
                           (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                    is_mut_param = true;
                }

                if (is_mut_param) {
                    if (v && v->state == VAR_STATE_DROPPED) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the dropped object", "Dropped at",
                            v->drop_span, insn->bb ? insn->bb->func : NULL,
                            decl);
                    } else if (v && v->state == VAR_STATE_BORROWED) {
                        osc_report_diagnostic(
                            ctx, insn->span, "Cannot move borrowed object",
                            NULL, (struct osc_span){ 0 },
                            insn->bb ? insn->bb->func : NULL, decl);
                    }
                    dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                           insn->span);
                }
            }
            arg_idx++;
        }
        break;

    case OP_CLEANUP:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            dataflow_state_set_var(state, decl, VAR_STATE_DROPPED, insn->span);
        }
        break;

    case OP_SCOPE_END:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                v = dataflow_state_get_var(state, decl);
                if (v && v->state == VAR_STATE_SET) {
                    osc_report_diagnostic(
                        ctx, insn->span,
                        "Should release the end-of-life object", "Set at",
                        v->set_span, insn->bb ? insn->bb->func : NULL, decl);
                }
            }
        }
        break;

    case OP_RET:
        if (insn->src1.kind == CFG_OPR_VAR && insn->src1.var_decl) {
            decl = insn->src1.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                osc_report_diagnostic(
                    ctx, insn->span,
                    "Return the borrowed object which doesn't belong to this function",
                    NULL, (struct osc_span){ 0 },
                    insn->bb ? insn->bb->func : NULL, decl);
            } else if (v && v->state == VAR_STATE_BORROWED) {
                osc_report_diagnostic(
                    ctx, insn->span,
                    "Return the borrowed object which doesn't belong to this function",
                    NULL, (struct osc_span){ 0 },
                    insn->bb ? insn->bb->func : NULL, decl);
            }
            if (v && v->state == VAR_STATE_DROPPED) {
                osc_report_diagnostic(
                    ctx, insn->span, "Return the dropped object", "Dropped at",
                    v->drop_span, insn->bb ? insn->bb->func : NULL, decl);
            }
            if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                       insn->span);
            }
        }
        break;

    default:
        break;
    }
}

/**
 * osc_attr_join - Confluence operator merging predecessor basic block facts
 * @dst: Mutable target state container
 * @src: Incoming predecessor state container
 */
static void osc_attr_join(struct dataflow_state *dst,
                          const struct dataflow_state *src)
{
    struct dataflow_var_state *v_src = NULL;
    struct dataflow_var_state *v_dst = NULL;

    if (!dst || !src)
        return;

    list_for_each_entry (v_src, &src->var_states, node) {
        v_dst = dataflow_state_get_var(dst, v_src->decl);
        if (!v_dst) {
            dataflow_state_set_var(dst, v_src->decl, v_src->state,
                                   (v_src->state == VAR_STATE_DROPPED) ?
                                       v_src->drop_span :
                                       v_src->set_span);
            v_dst = dataflow_state_get_var(dst, v_src->decl);
            if (v_dst) {
                v_dst->set_span = v_src->set_span;
                v_dst->drop_span = v_src->drop_span;
            }
        } else if (v_dst->state != v_src->state) {
            /* If dropped along any path, mark dropped (dropped dominates) */
            if (v_src->state == VAR_STATE_DROPPED ||
                v_dst->state == VAR_STATE_DROPPED) {
                if (v_dst->state != VAR_STATE_DROPPED &&
                    v_src->state == VAR_STATE_DROPPED)
                    v_dst->drop_span = v_src->drop_span;
                v_dst->state = VAR_STATE_DROPPED;
            } else if (v_src->state == VAR_STATE_SET ||
                       v_dst->state == VAR_STATE_SET) {
                if (v_dst->state != VAR_STATE_SET &&
                    v_src->state == VAR_STATE_SET)
                    v_dst->set_span = v_src->set_span;
                v_dst->state = VAR_STATE_SET;
            } else if (v_src->state == VAR_STATE_BORROWED ||
                       v_dst->state == VAR_STATE_BORROWED) {
                v_dst->state = VAR_STATE_BORROWED;
            }
        }
    }
}

/**
 * osc_attr_check_block_exit - Validate block boundary and function exit invariants
 * @ctx: Active compilation context
 * @bb: Basic block ending
 * @state: State facts at block exit
 */
static void osc_attr_check_block_exit(struct osc_context *ctx,
                                      struct basic_block *bb,
                                      struct dataflow_state *state)
{
    struct ast_decl *param = NULL;
    struct dataflow_var_state *v = NULL;
    struct osc_span rep_span = { 0 };

    if (!ctx || !bb || !state || !bb->func)
        return;

    /* Check function parameters at function exit */
    if (bb == bb->func->exit_bb || list_empty(&bb->succs)) {
        if (bb->func->decl && bb->func->decl->type &&
            bb->func->decl->type->is_function) {
            list_for_each_entry (param, &bb->func->decl->type->params, node) {
                if (param->type && (param->type->osc_attrs & ATTR_FLAGS_MUT)) {
                    v = dataflow_state_get_var(state, param);
                    if (v && v->state == VAR_STATE_SET) {
                        rep_span = (bb->func->exit_span.line > 0) ?
                                       bb->func->exit_span :
                                       param->span;
                        osc_report_diagnostic(
                            ctx, rep_span,
                            "Should release the end-of-life object", "Set at",
                            v->set_span, bb->func, param);
                    }
                }
            }
        }
    }
}

/**
 * osc_attr_finalize_function - Post-analysis function finalizer
 * @ctx: Active compilation context
 * @fn: Analyzed CFG function
 */
static void osc_attr_finalize_function(struct osc_context *ctx,
                                       struct cfg_function *fn)
{
    (void)ctx;
    (void)fn;
}

const struct osc_checker checker_osc_attr = {
    .name = "checker_osc_attr",
    .init_function = osc_attr_init,
    .transfer_instruction = osc_attr_transfer,
    .join_states = osc_attr_join,
    .check_block_exit = osc_attr_check_block_exit,
    .finalize_function = osc_attr_finalize_function,
};
