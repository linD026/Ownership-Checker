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
 * dump_object - Diagnostic helper to print object declaration context
 * @obj: Target object descriptor
 * @func: Enclosing function metadata
 * @place: Declaration site label (e.g. "argument", "scope")
 */
static void dump_object(struct object *obj, struct function *func,
                        const char *place)
{
    print("OSC NOTE: The object is declared as %s %s: ", func->object.id->name,
          place);
    if (obj->storage_class != sym_dump)
        print("%s ", token_name(obj->storage_class));
    if (obj->type != sym_dump) {
        print("%s ", token_name(obj->type));
        if (obj->type == sym_struct)
            print("%s ", obj->struct_id->name);
    }
    if (obj->attr & ATTR_FLAS_MASK) {
        if (obj->attr & ATTR_FLAGS_BRW)
            print("__brw ");
        if (obj->attr & ATTR_FLAGS_CLONE)
            print("__clone ");
        if (obj->attr & ATTR_FLAGS_MUT)
            print("__mut ");
    }
    if (obj->is_ptr)
        print("*");
    if (obj->id)
        print("%s", obj->id->name);
    print("\n");
}

/* checker list */

typedef int (*checker_t)(struct scan_file_control *, struct variable *,
                         struct object *);

/**
 * is_writable - Legacy verification of assignment permissions
 * @sfc: Active scanning control context
 * @var: Variable instance being written to
 * @orig_obj: Source object descriptor
 *
 * Return: 0 if valid, negative errno if borrowed or already dropped.
 */
static int is_writable(struct scan_file_control *sfc, struct variable *var,
                       struct object *orig_obj)
{
    struct object *obj = &var->object;
    /*
     * For the borrow attribute, we should only check the object
     * instead of ptr. For example, we should check:
     * 
     *      *brw_ptr = ... ;
     * 
     * not the following:
     *
     *      brw_ptr = ... ;
     *
     */
    if ((obj->attr & ATTR_FLAGS_BRW) && !(obj->attr & ATTR_FLAGS_MUT)) {
        if (orig_obj->is_ptr) {
            bad(sfc, "Don't write to the borrowed object");
            return -1;
        }
    }
    if ((obj->attr & ATTR_FLAGS_MUT) &&
        (var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Don't write to the dropped object");
        bad_on_dropped_info(sfc, &var->ptr_info.dropped_info);
        return -1;
    }
    // TODO: To compatible with the normal C,
    // we only check the pointer which has self-defined attributes
    if (0 && !(obj->attr & ATTR_FLAGS_MUT)) {
        bad(sfc, "Don't write to the immutable object");
        return -1;
    }

    return 0;
}

/**
 * is_owned - Legacy check ensuring returned object is actively owned
 * @sfc: Active scanning control context
 * @var: Variable instance being returned
 * @unused: Unused parameter for checker_t signature
 *
 * Return: 0 if valid, negative errno if borrowed or already dropped.
 */
static int is_owned(struct scan_file_control *sfc, struct variable *var,
                    struct object *unused)
{
    struct object *obj = &var->object;

    if ((obj->attr & ATTR_FLAGS_BRW) && !(obj->attr & ATTR_FLAGS_MUT)) {
        bad(sfc,
            "Return the borrowed object which doesn't belong to this function");
        return -1;
    }
    if ((obj->attr & ATTR_FLAGS_MUT) &&
        (var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Return the dropped object");
        bad_on_dropped_info(sfc, &var->ptr_info.dropped_info);
        return -1;
    }

    return 0;
}

/**
 * is_dropped - Legacy check verifying end-of-life object has been released
 * @sfc: Active scanning control context
 * @var: Variable instance exiting scope
 * @unused: Unused parameter for checker_t signature
 *
 * Return: 0 if valid, negative errno if mutable pointer was not released.
 */
static int is_dropped(struct scan_file_control *sfc, struct variable *var,
                      struct object *unused)
{
    struct object *obj = &var->object;

    if ((obj->attr & ATTR_FLAGS_MUT) && (var->ptr_info.flags & PTR_INFO_SET) &&
        !(var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Should release the end-of-life object");
        bad_on_set_info(sfc, &var->ptr_info.set_info);
        return -1;
    }

    return 0;
}

/* Common helper functions */

/**
 * check_ok - Helper verifying an object and its struct members
 * @sfc: Active scanning control context
 * @var: Variable instance to verify
 * @obj: Target object descriptor
 * @checker: Checker predicate function
 *
 * Return: 0 if valid, negative errno on failure.
 */
static __always_inline int check_ok(struct scan_file_control *sfc,
                                    struct variable *var, struct object *obj,
                                    checker_t checker)
{
    if (!cmp_token(obj->id, var->object.id))
        return 0;
    if (var->object.type == sym_struct) {
        list_for_each (&var->struct_info.struct_head) {
            struct variable *tmp =
                container_of(curr, struct variable, struct_node);

            if (checker(sfc, tmp, obj)) {
                // TODO: show the struct member
                print("struct member dropped\n");
                return -1;
            }
        }
    }

    return checker(sfc, var, obj);
}

/**
 * check_ownership - Iterate parameters and scopes applying checker callback
 * @sfc: Active scanning control context
 * @obj: Target object descriptor
 * @checker: Checker predicate function
 *
 * Return: 0 if all instances pass, negative errno on violation.
 */
static __always_inline int check_ownership(struct scan_file_control *sfc,
                                           struct object *obj,
                                           checker_t checker)
{
    struct scope_iter_data iter;
    struct function *func = sfc->function;

    list_for_each (&func->parameter_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        if (check_ok(sfc, param, obj, checker)) {
            dump_object(&param->object, func, "argument");
            return -1;
        }
    }

    for_each_var_in_scopes (func, &iter) {
        struct variable *var = iter.var;
        if (check_ok(sfc, var, obj, checker)) {
            dump_object(&var->object, func, "scope");
            return -1;
        }
    }

    return 0;
}

/* The external functions called in src/parser.c */

#define DEFINE_CHECKER(name, checker)                           \
    int name(struct scan_file_control *sfc, struct object *obj) \
    {                                                           \
        return check_ownership(sfc, obj, checker);              \
    }

DEFINE_CHECKER(check_ownership_writable, is_writable)
DEFINE_CHECKER(check_ownership_owned, is_owned)
DEFINE_CHECKER(check_ownership_dropped, is_dropped)

/*
 * Pluggable CFG Dataflow Solver and State Lattice Implementation
 */

/**
 * dataflow_state_new - Allocate an empty dataflow state container
 * @ctx: Active compilation context
 * @fn: Target CFG function
 *
 * Return: Pointer to initialized struct dataflow_state.
 */
struct dataflow_state *dataflow_state_new(struct osc_context *ctx,
                                          struct cfg_function *fn)
{
    struct dataflow_state *state = NULL;

    BUG_ON(!ctx, "null osc_context");

    state = osc_arena_zalloc(&ctx->arena, sizeof(*state));
    BUG_ON(!state, "arena zalloc failed for dataflow_state");

    state->ctx = ctx;
    state->fn = fn;
    list_init(&state->var_states);

    return state;
}

/**
 * dataflow_state_clone - Deep-copy a dataflow state container
 * @ctx: Active compilation context
 * @src: Source state container
 *
 * Return: Fresh cloned struct dataflow_state allocated on context arena.
 */
struct dataflow_state *dataflow_state_clone(struct osc_context *ctx,
                                            const struct dataflow_state *src)
{
    struct dataflow_state *dst = NULL;
    struct dataflow_var_state *var = NULL;
    struct dataflow_var_state *new_var = NULL;

    if (!src)
        return NULL;

    dst = dataflow_state_new(ctx, src->fn);

    list_for_each_entry (var, &src->var_states, node) {
        new_var = osc_arena_zalloc(&ctx->arena, sizeof(*new_var));
        BUG_ON(!new_var, "arena zalloc failed for cloned dataflow_var_state");

        new_var->decl = var->decl;
        new_var->state = var->state;
        new_var->set_span = var->set_span;
        new_var->drop_span = var->drop_span;
        new_var->escaped_to = var->escaped_to;
        list_init(&new_var->node);
        list_add_tail(&new_var->node, &dst->var_states);
    }

    return dst;
}

/**
 * dataflow_state_get_var - Lookup the lattice fact for an AST variable
 * @state: Active dataflow state container
 * @decl: Target variable AST declaration
 *
 * Return: Matching struct dataflow_var_state if present, NULL otherwise.
 */
struct dataflow_var_state *dataflow_state_get_var(struct dataflow_state *state,
                                                  struct ast_decl *decl)
{
    struct dataflow_var_state *v = NULL;

    if (!state || !decl)
        return NULL;

    list_for_each_entry (v, &state->var_states, node) {
        if (v->decl == decl)
            return v;
    }

    return NULL;
}

/**
 * dataflow_state_set_var - Update or insert a variable lattice state
 * @state: Active dataflow state container
 * @decl: Target variable AST declaration
 * @val: New abstract value state (enum dataflow_var_val)
 * @span: Source span coordinates associated with the state change
 */
void dataflow_state_set_var(struct dataflow_state *state, struct ast_decl *decl,
                            int val, struct osc_span span)
{
    struct dataflow_var_state *v = NULL;

    if (!state || !decl)
        return;

    v = dataflow_state_get_var(state, decl);
    if (!v) {
        v = osc_arena_zalloc(&state->ctx->arena, sizeof(*v));
        BUG_ON(!v, "arena zalloc failed for dataflow_var_state");
        v->decl = decl;
        list_init(&v->node);
        list_add_tail(&v->node, &state->var_states);
    }

    v->state = val;
    if (val == VAR_STATE_SET)
        v->set_span = span;
    else if (val == VAR_STATE_DROPPED || val == VAR_STATE_ESCAPED)
        v->drop_span = span;
}

/**
 * dataflow_state_equal - Check whether two dataflow state containers are equivalent
 * @s1: First state container
 * @s2: Second state container
 *
 * Return: True if both states have matching variables and identical values.
 */
bool dataflow_state_equal(const struct dataflow_state *s1,
                          const struct dataflow_state *s2)
{
    struct dataflow_var_state *v1 = NULL;
    struct dataflow_var_state *v2 = NULL;
    size_t count1 = 0;
    size_t count2 = 0;

    if (s1 == s2)
        return true;
    if (!s1 || !s2)
        return false;

    list_for_each_entry (v1, &s1->var_states, node)
        count1++;

    list_for_each_entry (v2, &s2->var_states, node)
        count2++;

    if (count1 != count2)
        return false;

    list_for_each_entry (v1, &s1->var_states, node) {
        v2 = dataflow_state_get_var((struct dataflow_state *)s2, v1->decl);
        if (!v2 || v2->state != v1->state)
            return false;
    }

    return true;
}

/**
 * run_dataflow_analysis - Forward dataflow fixpoint solver
 * @ctx: Active compilation context
 * @fn: Target linearized CFG function
 * @checker: Pluggable checker pass providing transfer and join operators
 *
 * Drives iterative dataflow analysis across all basic blocks until
 * states converge to a fixpoint, invoking the checker's transfer and
 * validation callbacks.
 *
 * Return: 0 on convergence, negative errno on invalid arguments.
 */
int run_dataflow_analysis(struct osc_context *ctx, struct cfg_function *fn,
                          const struct osc_checker *checker)
{
    struct basic_block **worklist = NULL;
    bool *in_worklist = NULL;
    struct basic_block *bb = NULL;
    struct instruction *insn = NULL;
    struct cfg_edge *edge = NULL;
    struct dataflow_state *in_state = NULL;
    struct dataflow_state *curr_state = NULL;
    size_t head = 0;
    size_t tail = 0;
    size_t max_blocks = 0;
    size_t capacity = 0;

    if (!ctx || !fn || !checker)
        return -EINVAL;

    max_blocks = fn->next_bb_id + 1;
    capacity = max_blocks * 8; /* ample capacity for loops to reach fixpoint */

    worklist = osc_arena_zalloc(&ctx->arena, capacity * sizeof(*worklist));
    BUG_ON(!worklist, "arena zalloc failed for worklist");

    in_worklist =
        osc_arena_zalloc(&ctx->arena, max_blocks * sizeof(*in_worklist));
    BUG_ON(!in_worklist, "arena zalloc failed for in_worklist");

    /* Initialize entry block */
    if (fn->entry_bb) {
        fn->entry_bb->df_in = dataflow_state_new(ctx, fn);
        if (checker->init_function)
            checker->init_function(ctx, fn, fn->entry_bb->df_in);

        worklist[tail++] = fn->entry_bb;
        in_worklist[fn->entry_bb->id] = true;
    }

    /* Iterate until worklist is empty */
    while (head < tail) {
        bb = worklist[head++];
        in_worklist[bb->id] = false;

        /* Merge incoming predecessor states for non-entry blocks */
        if (bb != fn->entry_bb) {
            in_state = NULL;
            list_for_each_entry (edge, &bb->preds, pred_node) {
                if (edge->src && edge->src->df_out) {
                    if (!in_state) {
                        in_state = dataflow_state_clone(ctx, edge->src->df_out);
                    } else if (checker->join_states) {
                        checker->join_states(in_state, edge->src->df_out);
                    }
                }
            }
            if (in_state)
                bb->df_in = in_state;
        }

        if (!bb->df_in)
            bb->df_in = dataflow_state_new(ctx, fn);

        /* Apply transfer function sequentially across instructions */
        curr_state = dataflow_state_clone(ctx, bb->df_in);

        list_for_each_entry (insn, &bb->instructions, node) {
            if (checker->transfer_instruction)
                checker->transfer_instruction(ctx, insn, curr_state);
        }

        /* Check block exit invariants */
        if (checker->check_block_exit)
            checker->check_block_exit(ctx, bb, curr_state);

        /* If out state changed, schedule successors for re-analysis */
        if (!bb->df_out || !dataflow_state_equal(bb->df_out, curr_state)) {
            bb->df_out = curr_state;

            list_for_each_entry (edge, &bb->succs, succ_node) {
                if (edge->dst && !in_worklist[edge->dst->id] &&
                    tail < capacity) {
                    worklist[tail++] = edge->dst;
                    in_worklist[edge->dst->id] = true;
                }
            }
        }
    }

    /* Final validation pass */
    if (checker->finalize_function)
        checker->finalize_function(ctx, fn);

    return 0;
}

/**
 * osc_report_span - Diagnostic reporter for semantic ownership violations
 * @ctx: Active compilation context
 * @span: Source coordinates of the violation
 * @msg: Description of violation
 */
static void osc_report_span(struct osc_context *ctx, struct osc_span span,
                            const char *msg)
{
    const char *fname = "<input>";

    if (!ctx)
        return;

    ctx->error_count++;

    if (ctx->fi && ctx->fi->name[0])
        fname = ctx->fi->name;

    print("\e[1m\e[31mOSC ERROR\e[0m\e[0m: \e[1m%s\e[0m\n", msg);
    print("    \e[36m|->\e[0m %s:%zu:%u\n", fname, span.line, span.col);
}

/**
 * osc_find_decl - Find a top-level AST declaration by identifier symbol
 * @ctx: Active compilation context
 * @sym: Target identifier symbol
 *
 * Return: Matching struct ast_decl pointer if found, NULL otherwise.
 */
struct ast_decl *osc_find_decl(struct osc_context *ctx, struct symbol *sym)
{
    struct ast_decl *decl = NULL;

    if (!ctx || !ctx->tu || !sym)
        return NULL;

    list_for_each_entry (decl, &ctx->tu->decls, node) {
        if (decl->id == sym)
            return decl;
    }

    return NULL;
}

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
                    osc_report_span(ctx, insn->span,
                                    "Don't write to the borrowed object");
                }
                if (v && v->state == VAR_STATE_DROPPED) {
                    osc_report_span(ctx, insn->span,
                                    "Don't write to the dropped object");
                }
            } else {
                /* Pointer or value assignment: decl = ... */
                if (v && v->state == VAR_STATE_DROPPED) {
                    osc_report_span(ctx, insn->span,
                                    "Don't write to the dropped object");
                }

                if (insn->src1.kind == CFG_OPR_CONST &&
                    insn->src1.const_val == 0) {
                    /* Disarming / clearing pointer: decl = NULL */
                    if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_MUT))
                        dataflow_state_set_var(state, decl, VAR_STATE_DROPPED,
                                               insn->span);
                } else if (insn->src1.kind == CFG_OPR_VAR &&
                           insn->src1.var_decl) {
                    /* Moving ownership from src to decl */
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
                        osc_report_span(ctx, insn->span,
                                        "Don't write to the dropped object");
                    } else if (v && v->state == VAR_STATE_BORROWED) {
                        osc_report_span(ctx, insn->span,
                                        "Cannot move borrowed object");
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
                    osc_report_span(ctx, decl->span,
                                    "Should release the end-of-life object");
                }
            }
        }
        break;

    case OP_RET:
        if (insn->src1.kind == CFG_OPR_VAR && insn->src1.var_decl) {
            decl = insn->src1.var_decl;
            v = dataflow_state_get_var(state, decl);

            if (decl->type && (decl->type->osc_attrs & ATTR_FLAGS_BRW)) {
                osc_report_span(
                    ctx, insn->span,
                    "Return the borrowed object which doesn't belong to this function");
            } else if (v && v->state == VAR_STATE_BORROWED) {
                osc_report_span(
                    ctx, insn->span,
                    "Return the borrowed object which doesn't belong to this function");
            }
            if (v && v->state == VAR_STATE_DROPPED) {
                osc_report_span(ctx, insn->span, "Return the dropped object");
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
                                   v_src->set_span);
        } else if (v_dst->state != v_src->state) {
            /* If dropped along any path, mark dropped (dropped dominates) */
            if (v_src->state == VAR_STATE_DROPPED ||
                v_dst->state == VAR_STATE_DROPPED) {
                v_dst->state = VAR_STATE_DROPPED;
            } else if (v_src->state == VAR_STATE_SET ||
                       v_dst->state == VAR_STATE_SET) {
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
                        osc_report_span(
                            ctx, param->span,
                            "Should release the end-of-life object");
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
