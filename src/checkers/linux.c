#include <osc/parser.h>
#include <osc/debug.h>
#include <osc/check_list.h>
#include <osc/check_ownership.h>
#include <osc/arena.h>
#include <osc/context.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <stdio.h>

/*
 * Model 2: Linux Kernel Resource & Hybrid RAII Checker Pass
 */

/**
 * is_kernel_allocator - Determine if a function symbol is a standard allocator
 * @sym: Target callee symbol
 *
 * Return: True if callee is a recognized allocator (kmalloc, alloc_page, etc.).
 */
static bool is_kernel_allocator(const struct symbol *sym)
{
    const char *name = NULL;

    if (!sym || !sym->name)
        return false;

    name = sym->name;
    return (!strcmp(name, "kmalloc") || !strcmp(name, "kzalloc") ||
            !strcmp(name, "kvmalloc") || !strcmp(name, "kcalloc") ||
            !strcmp(name, "vmalloc") || !strcmp(name, "vzalloc") ||
            !strcmp(name, "alloc_page") || !strcmp(name, "alloc_pages") ||
            !strcmp(name, "get_free_page") || !strcmp(name, "malloc") ||
            !strcmp(name, "calloc") || !strcmp(name, "kmem_cache_alloc") ||
            !strcmp(name, "kmem_cache_zalloc"));
}

/**
 * is_kernel_deallocator - Determine if a function symbol is a standard deallocator
 * @sym: Target callee symbol
 *
 * Return: True if callee is a recognized deallocator (kfree, put_page, etc.).
 */
static bool is_kernel_deallocator(const struct symbol *sym)
{
    const char *name = NULL;

    if (!sym || !sym->name)
        return false;

    name = sym->name;
    return (!strcmp(name, "kfree") || !strcmp(name, "kvfree") ||
            !strcmp(name, "vfree") || !strcmp(name, "put_page") ||
            !strcmp(name, "free_page") || !strcmp(name, "free_pages") ||
            !strcmp(name, "__free_pages") || !strcmp(name, "free") ||
            !strcmp(name, "kmem_cache_free"));
}

/**
 * is_intrusive_sink - Determine if a function is an intrusive collection sink
 * @sym: Target callee symbol
 *
 * Return: True if callee links an intrusive node into a collection.
 */
static bool is_intrusive_sink(const struct symbol *sym)
{
    const char *name = NULL;

    if (!sym || !sym->name)
        return false;

    name = sym->name;
    return (!strcmp(name, "list_add") || !strcmp(name, "list_add_tail") ||
            !strcmp(name, "hlist_add_head") ||
            !strcmp(name, "hlist_add_tail") ||
            !strcmp(name, "hlist_add_behind") || !strcmp(name, "rb_link_node"));
}

/**
 * linux_res_init - Initialize function parameter lifecycle states
 * @ctx: Active compilation context
 * @fn: Target CFG function
 * @state: Initial dataflow state for entry basic block
 */
static void linux_res_init(struct osc_context *ctx, struct cfg_function *fn,
                           struct dataflow_state *state)
{
    struct ast_decl *param = NULL;

    if (!ctx || !fn || !state)
        return;

    if (fn->decl && fn->decl->type && fn->decl->type->is_function) {
        list_for_each_entry (param, &fn->decl->type->params, node) {
            if (param->cleanup_fn) {
                dataflow_state_set_var(state, param, VAR_STATE_SET,
                                       param->span);
            }
        }
    }
}

/**
 * linux_res_transfer - Transfer function evaluating kernel resource operations
 * @ctx: Active compilation context
 * @insn: Instruction being evaluated
 * @state: Mutable block dataflow state
 */
static void linux_res_transfer(struct osc_context *ctx,
                               struct instruction *insn,
                               struct dataflow_state *state)
{
    struct ast_decl *decl = NULL;
    struct ast_decl *src_decl = NULL;
    struct cfg_arg_node *arg1 = NULL;
    struct dataflow_var_state *v = NULL;
    struct dataflow_var_state *v_src = NULL;

    if (!ctx || !insn || !state)
        return;

    switch (insn->opcode) {
    case OP_ALLOC:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            if (decl->cleanup_fn) {
                dataflow_state_set_var(state, decl, VAR_STATE_UNTRACKED,
                                       insn->span);
            }
        }
        break;

    case OP_CALL:
        if (is_kernel_allocator(insn->callee_sym)) {
            if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
                dataflow_state_set_var(state, insn->target.var_decl,
                                       VAR_STATE_SET, insn->span);
            } else if (insn->target.kind == CFG_OPR_TEMP) {
                if (state->temp_allocated &&
                    insn->target.temp_id < state->temp_allocated_size) {
                    state->temp_allocated[insn->target.temp_id] = true;
                }
            }
        } else if (is_kernel_deallocator(insn->callee_sym)) {
            if (!list_empty(&insn->args)) {
                arg1 = list_first_entry(&insn->args, struct cfg_arg_node, node);
                if (arg1 && arg1->op.kind == CFG_OPR_VAR && arg1->op.var_decl) {
                    decl = arg1->op.var_decl;
                    v = dataflow_state_get_var(state, decl);

                    if (v && v->state == VAR_STATE_DROPPED) {
                        osc_report_span(
                            ctx, insn->span,
                            "Double-free detected: object already released");
                    }
                    dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                           insn->span);
                }
            }
        } else if (is_intrusive_sink(insn->callee_sym)) {
            if (!list_empty(&insn->args)) {
                arg1 = list_first_entry(&insn->args, struct cfg_arg_node, node);
                decl = NULL;
                if (arg1 && arg1->proj_var)
                    decl = arg1->proj_var;
                else if (arg1 && arg1->op.kind == CFG_OPR_VAR &&
                         arg1->op.var_decl)
                    decl = arg1->op.var_decl;

                if (decl) {
                    if (decl->cleanup_fn) {
                        dataflow_state_set_var(state, decl, VAR_STATE_ESCAPED,
                                               insn->span);
                    } else {
                        dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                               insn->span);
                    }
                }
            }
        }
        break;

    case OP_STORE:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (insn->src1.kind == CFG_OPR_CONST && insn->src1.const_val == 0) {
                /* Disarming pointer to NULL: safely disarms destructor */
                dataflow_state_set_var(state, decl, VAR_STATE_UNTRACKED,
                                       insn->span);
            } else if (insn->src1.kind == CFG_OPR_TEMP) {
                if (state->temp_allocated &&
                    insn->src1.temp_id < state->temp_allocated_size &&
                    state->temp_allocated[insn->src1.temp_id]) {
                    dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                           insn->span);
                }
            } else if (insn->src1.kind == CFG_OPR_VAR && insn->src1.var_decl) {
                src_decl = insn->src1.var_decl;
                v_src = dataflow_state_get_var(state, src_decl);
                if (v_src && v_src->state == VAR_STATE_SET) {
                    dataflow_state_set_var(state, src_decl, VAR_STATE_DROPPED,
                                           insn->span);
                    dataflow_state_set_var(state, decl, VAR_STATE_SET,
                                           insn->span);
                }
            }
        }
        break;

    case OP_CLEANUP:
        if (insn->target.kind == CFG_OPR_VAR && insn->target.var_decl) {
            decl = insn->target.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (v && v->state == VAR_STATE_ESCAPED) {
                osc_report_span(
                    ctx, insn->span,
                    "Destructor-after-transfer detected (escaped object not disarmed)");
            } else if (v && v->state == VAR_STATE_DROPPED) {
                osc_report_span(
                    ctx, insn->span,
                    "Hybrid error-path double-free: destructor fires on already-freed object");
            }
            dataflow_state_set_var(state, decl, VAR_STATE_DROPPED, insn->span);
        }
        break;

    case OP_RET:
        if (insn->src1.kind == CFG_OPR_VAR && insn->src1.var_decl) {
            decl = insn->src1.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (decl->cleanup_fn) {
                dataflow_state_set_var(state, decl, VAR_STATE_ESCAPED,
                                       insn->span);
            } else if (v && v->state == VAR_STATE_SET) {
                dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                       insn->span);
            }
        }

        /* Check for unreleased resource leaks at this return statement */
        list_for_each_entry (v, &state->var_states, node) {
            if (v->state == VAR_STATE_SET && v->decl && !v->decl->cleanup_fn) {
                osc_report_span(
                    ctx, insn->span,
                    "Resource leak along return path: object was not released");
            }
        }
        break;

    default:
        break;
    }
}

/**
 * linux_res_join - Confluence operator merging predecessor basic block facts
 * @dst: Mutable target state container
 * @src: Incoming predecessor state container
 */
static void linux_res_join(struct dataflow_state *dst,
                           const struct dataflow_state *src)
{
    struct dataflow_var_state *v_src = NULL;
    struct dataflow_var_state *v_dst = NULL;
    size_t i = 0;

    if (!dst || !src)
        return;

    if (dst->temp_allocated && src->temp_allocated &&
        dst->temp_allocated_size == src->temp_allocated_size) {
        for (i = 0; i < dst->temp_allocated_size; i++) {
            if (src->temp_allocated[i])
                dst->temp_allocated[i] = true;
        }
    }

    list_for_each_entry (v_src, &src->var_states, node) {
        v_dst = dataflow_state_get_var(dst, v_src->decl);
        if (!v_dst) {
            dataflow_state_set_var(dst, v_src->decl, v_src->state,
                                   v_src->set_span);
        } else if (v_dst->state != v_src->state) {
            if (v_src->state == VAR_STATE_ESCAPED ||
                v_dst->state == VAR_STATE_ESCAPED) {
                v_dst->state = VAR_STATE_ESCAPED;
            } else if (v_src->state == VAR_STATE_DROPPED ||
                       v_dst->state == VAR_STATE_DROPPED) {
                v_dst->state = VAR_STATE_DROPPED;
            } else if (v_src->state == VAR_STATE_SET ||
                       v_dst->state == VAR_STATE_SET) {
                v_dst->state = VAR_STATE_SET;
            }
        }
    }
}

/**
 * linux_res_check_block_exit - Validate block boundary and detect resource leaks
 * @ctx: Active compilation context
 * @bb: Basic block ending
 * @state: State facts at block exit
 */
static void linux_res_check_block_exit(struct osc_context *ctx,
                                       struct basic_block *bb,
                                       struct dataflow_state *state)
{
    struct dataflow_var_state *v = NULL;

    if (!ctx || !bb || !state || !bb->func)
        return;

    /* Detect unreleased allocations along exit or return blocks */
    if (bb == bb->func->exit_bb || list_empty(&bb->succs)) {
        list_for_each_entry (v, &state->var_states, node) {
            if (v->state == VAR_STATE_SET && v->decl && !v->decl->cleanup_fn) {
                osc_report_span(
                    ctx, v->decl->span,
                    "Resource leak along path: object was not released");
            }
        }
    }
}

/**
 * linux_res_finalize - Post-analysis function finalizer
 * @ctx: Active compilation context
 * @fn: Analyzed CFG function
 */
static void linux_res_finalize(struct osc_context *ctx, struct cfg_function *fn)
{
    (void)ctx;
    (void)fn;
}

const struct osc_checker checker_linux_resource = {
    .name = "checker_linux_resource",
    .init_function = linux_res_init,
    .transfer_instruction = linux_res_transfer,
    .join_states = linux_res_join,
    .check_block_exit = linux_res_check_block_exit,
    .finalize_function = linux_res_finalize,
};
