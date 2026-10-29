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
    struct ast_decl *mem_decl = NULL;
    struct ast_decl *rep_decl = NULL;
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
            if (insn->container_member) {
                mem_decl = osc_find_struct_member(ctx, decl->type,
                                                  insn->container_member);
                v = dataflow_state_get_member(state, decl,
                                              insn->container_member);

                if (insn->is_deref) {
                    /* Dereference store: *(decl->member) = ... */
                    if (mem_decl && mem_decl->type &&
                        (mem_decl->type->osc_attrs & ATTR_FLAGS_BRW) &&
                        !(mem_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the borrowed object", NULL,
                            (struct osc_span){ 0 },
                            insn->bb ? insn->bb->func : NULL, mem_decl);
                    }
                    if (v && v->state == VAR_STATE_DROPPED) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the dropped object", "Dropped at",
                            v->drop_span, insn->bb ? insn->bb->func : NULL,
                            mem_decl ? mem_decl : decl);
                    }
                } else {
                    /* Member pointer or value assignment: decl->member = ... */
                    if (insn->src1.kind == CFG_OPR_CONST &&
                        insn->src1.const_val == 0) {
                        /* Disarming / clearing member: decl->member = NULL */
                        if (mem_decl && mem_decl->type &&
                            (mem_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                            dataflow_state_set_member(
                                state, decl, insn->container_member, mem_decl,
                                VAR_STATE_DROPPED, insn->span);
                        }
                    } else if (insn->src1.kind == CFG_OPR_VAR &&
                               insn->src1.var_decl) {
                        /* Moving ownership from src to decl->member */
                        v_src =
                            dataflow_state_get_var(state, insn->src1.var_decl);
                        if (v_src && v_src->state == VAR_STATE_DROPPED) {
                            osc_report_diagnostic(
                                ctx, insn->span,
                                "Don't write to the dropped object",
                                "Dropped at", v_src->drop_span,
                                insn->bb ? insn->bb->func : NULL,
                                insn->src1.var_decl);
                        }
                        if (insn->src1.var_decl->type &&
                            (insn->src1.var_decl->type->osc_attrs &
                             ATTR_FLAGS_MUT)) {
                            dataflow_state_set_var(state, insn->src1.var_decl,
                                                   VAR_STATE_DROPPED,
                                                   insn->span);
                        }
                        if (mem_decl && mem_decl->type &&
                            (mem_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                            dataflow_state_set_member(
                                state, decl, insn->container_member, mem_decl,
                                VAR_STATE_SET, insn->span);
                        } else if (mem_decl && mem_decl->type &&
                                   (mem_decl->type->osc_attrs &
                                    ATTR_FLAGS_BRW)) {
                            dataflow_state_set_member(
                                state, decl, insn->container_member, mem_decl,
                                VAR_STATE_BORROWED, insn->span);
                        }
                    } else {
                        /* Setting member from expression (e.g. data->ptrA = &a) */
                        if (mem_decl && mem_decl->type &&
                            (mem_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                            dataflow_state_set_member(
                                state, decl, insn->container_member, mem_decl,
                                VAR_STATE_SET, insn->span);
                        } else if (mem_decl && mem_decl->type &&
                                   (mem_decl->type->osc_attrs &
                                    ATTR_FLAGS_BRW)) {
                            dataflow_state_set_member(
                                state, decl, insn->container_member, mem_decl,
                                VAR_STATE_BORROWED, insn->span);
                        }
                    }
                }
            } else {
                /* Non-member base variable store: decl = ... */
                v = dataflow_state_get_var(state, decl);

                if (insn->is_deref) {
                    /* Dereference store: *decl = ... */
                    if (decl->type &&
                        (decl->type->osc_attrs & ATTR_FLAGS_BRW) &&
                        !(decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the borrowed object", NULL,
                            (struct osc_span){ 0 },
                            insn->bb ? insn->bb->func : NULL, decl);
                    }
                    if (v && v->state == VAR_STATE_DROPPED) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the dropped object", "Dropped at",
                            v->drop_span, insn->bb ? insn->bb->func : NULL,
                            decl);
                    }
                } else {
                    /* Pointer or value assignment: decl = ... */
                    if (insn->src1.kind == CFG_OPR_CONST &&
                        insn->src1.const_val == 0) {
                        /* Disarming / clearing pointer: decl = NULL */
                        if (decl->type &&
                            (decl->type->osc_attrs & ATTR_FLAGS_MUT))
                            dataflow_state_set_var(
                                state, decl, VAR_STATE_DROPPED, insn->span);
                    } else if (insn->src1.kind == CFG_OPR_VAR &&
                               insn->src1.var_decl) {
                        /* Moving ownership from src to decl */
                        v_src =
                            dataflow_state_get_var(state, insn->src1.var_decl);
                        if (v_src && v_src->state == VAR_STATE_DROPPED) {
                            osc_report_diagnostic(
                                ctx, insn->span,
                                "Don't write to the dropped object",
                                "Dropped at", v_src->drop_span,
                                insn->bb ? insn->bb->func : NULL,
                                insn->src1.var_decl);
                        }
                        if (insn->src1.var_decl->type &&
                            (insn->src1.var_decl->type->osc_attrs &
                             ATTR_FLAGS_MUT)) {
                            dataflow_state_set_var(state, insn->src1.var_decl,
                                                   VAR_STATE_DROPPED,
                                                   insn->span);
                        }
                        if (decl->type &&
                            (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                            dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                                   insn->span);
                        } else if (decl->type &&
                                   (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                            dataflow_state_set_var(
                                state, decl, VAR_STATE_BORROWED, insn->span);
                        }
                    } else {
                        /* Initializing / setting decl */
                        if (decl->type &&
                            (decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                            dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                                   insn->span);
                        } else if (decl->type &&
                                   (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                            dataflow_state_set_var(
                                state, decl, VAR_STATE_BORROWED, insn->span);
                        }
                    }
                }
            }
        }
        break;

    case OP_CALL:
        callee_decl = osc_find_decl(ctx, insn->callee_sym);
        arg_idx = 0;

        list_for_each_entry (arg, &insn->args, node) {
            decl = NULL;
            mem_decl = NULL;
            rep_decl = NULL;
            v = NULL;

            if (arg->proj_var && arg->proj_member) {
                decl = arg->proj_var;
                mem_decl =
                    osc_find_struct_member(ctx, decl->type, arg->proj_member);
                v = dataflow_state_get_member(state, decl, arg->proj_member);
                rep_decl = mem_decl ? mem_decl : decl;
            } else if (arg->op.kind == CFG_OPR_VAR && arg->op.var_decl) {
                decl = arg->op.var_decl;
                v = dataflow_state_get_var(state, decl);
                rep_decl = decl;
            }

            if (decl && rep_decl) {
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
                    } else if (rep_decl->type &&
                               (rep_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        is_mut_param = true;
                    }
                } else if (rep_decl->type &&
                           (rep_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                    is_mut_param = true;
                }

                if (is_mut_param) {
                    if (v && v->state == VAR_STATE_DROPPED) {
                        osc_report_diagnostic(
                            ctx, insn->span,
                            "Don't write to the dropped object", "Dropped at",
                            v->drop_span, insn->bb ? insn->bb->func : NULL,
                            rep_decl);
                    } else if (v && v->state == VAR_STATE_BORROWED) {
                        osc_report_diagnostic(
                            ctx, insn->span, "Cannot move borrowed object",
                            NULL, (struct osc_span){ 0 },
                            insn->bb ? insn->bb->func : NULL, rep_decl);
                    }
                    if (arg->proj_var && arg->proj_member) {
                        dataflow_state_set_member(state, decl, arg->proj_member,
                                                  mem_decl, VAR_STATE_DROPPED,
                                                  insn->span);
                    } else {
                        dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                               insn->span);
                    }
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
                    v->state = VAR_STATE_DROPPED;
                }
            }
            list_for_each_entry (v, &state->var_states, node) {
                if (v->decl == decl && v->member != NULL &&
                    v->state == VAR_STATE_SET) {
                    osc_report_diagnostic(
                        ctx, insn->span,
                        "Should release the end-of-life object", "Set at",
                        v->set_span, insn->bb ? insn->bb->func : NULL,
                        v->mem_decl ? v->mem_decl : decl);
                    v->state = VAR_STATE_DROPPED;
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
        if (v_src->member)
            v_dst = dataflow_state_get_member(dst, v_src->decl, v_src->member);
        else
            v_dst = dataflow_state_get_var(dst, v_src->decl);

        if (!v_dst) {
            if (v_src->member) {
                dataflow_state_set_member(dst, v_src->decl, v_src->member,
                                          v_src->mem_decl, v_src->state,
                                          (v_src->state == VAR_STATE_DROPPED) ?
                                              v_src->drop_span :
                                              v_src->set_span);
                v_dst =
                    dataflow_state_get_member(dst, v_src->decl, v_src->member);
            } else {
                dataflow_state_set_var(dst, v_src->decl, v_src->state,
                                       (v_src->state == VAR_STATE_DROPPED) ?
                                           v_src->drop_span :
                                           v_src->set_span);
                v_dst = dataflow_state_get_var(dst, v_src->decl);
            }
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
    struct dataflow_var_state *v = NULL;
    struct osc_span rep_span = { 0 };

    if (!ctx || !bb || !state || !bb->func)
        return;

    /* Check unreleased objects and struct members at function exit */
    if (bb == bb->func->exit_bb || list_empty(&bb->succs)) {
        rep_span = (bb->func->exit_span.line > 0) ? bb->func->exit_span :
                                                    bb->func->decl->span;

        list_for_each_entry (v, &state->var_states, node) {
            if (v->state == VAR_STATE_SET) {
                if (v->member != NULL) {
                    if (v->mem_decl && v->mem_decl->type &&
                        (v->mem_decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                        osc_report_diagnostic(
                            ctx, rep_span,
                            "Should release the end-of-life object", "Set at",
                            v->set_span, bb->func, v->mem_decl);
                        v->state = VAR_STATE_DROPPED;
                    }
                } else if (v->decl && v->decl->type &&
                           (v->decl->type->osc_attrs & ATTR_FLAGS_MUT)) {
                    osc_report_diagnostic(
                        ctx, rep_span, "Should release the end-of-life object",
                        "Set at", v->set_span, bb->func, v->decl);
                    v->state = VAR_STATE_DROPPED;
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
