#include <osc/fork.h>
#include <osc/parser.h>
#include <osc/object_ptr.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Before we entry the if scope, we should fork the current function state,
 * so that the subsequent scope (else if, else) can restore back and run the
 * previous state.
 *
 *      #1=@fork
 *      if (...) {
 *          #2
 *      } else {
 *          @restore=#1, #3=@fork=#1 (copy the #1)
 *          #3
 *      }
 *
 *      @join #1, #2, #3
 */
struct function_state *fork_function_state(struct function *func)
{
    struct scope *scope = NULL;
    struct function *dst, *src = func;
    struct function_state *fs = malloc(sizeof(struct function_state));

    BUG_ON(!fs, "malloc");

    pr_debug("fork scope start\n");
    debug_function(src);

    fs->id = src->nr_state++;
    dst = &fs->function;
    list_init(&dst->func_scope_head);
    copy_object(&dst->object, &src->object);
    list_init(&dst->parameter_head);
    dst->nr_state = -1;
    list_init(&dst->state_head);
    list_init(&dst->node);

    list_for_each (&src->parameter_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        struct variable *new_var = var_alloc();

        copy_variable(new_var, param);
        list_add_tail(&new_var->parameter_node, &dst->parameter_head);
        debug_variable(new_var, "fork param");
    }

    list_for_each_entry (scope, &src->func_scope_head, func_scope_node) {
        struct variable *var = NULL;
        struct scope *new_scope = malloc(sizeof(struct scope));
        BUG_ON(!scope, "malloc");

        list_init(&new_scope->scope_var_head);
        list_init(&new_scope->func_scope_node);

        pr_debug("fork scope\n");

        for_each_var (scope, var) {
            struct variable *new_var = var_alloc();

            copy_variable(new_var, var);
            list_add_tail(&new_var->scope_node, &new_scope->scope_var_head);
            debug_variable(new_var, "fork var");
        }

        list_add_tail(&new_scope->func_scope_node, &dst->func_scope_head);
    }

    list_add_tail(&fs->state_node, &src->state_head);

    pr_debug("fork scope end\n");

    return fs;
}

void switch_function_state(struct scan_file_control *sfc,
                           struct function_state *new)
{
    sfc->function = &new->function;
}

void fork_and_switch_function_state(struct scan_file_control *sfc)
{
    switch_function_state(sfc, fork_function_state(sfc->real_function));
}

void restore_function_state(struct scan_file_control *sfc)
{
    sfc->function = sfc->real_function;
}

// TODO: standardize the rule
static void join_variable(struct variable *real, struct variable *tmp)
{
    if (cmp_object(&tmp->object, &real->object)) {
        if (tmp->ptr_info.flags & PTR_INFO_DROPPED &&
            real->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)) {
            pr_debug("drop the variable\n");
            debug_variable(tmp, "dropped var");
            __record_ptr_info(&real->ptr_info.dropped_info,
                              tmp->ptr_info.dropped_info.buffer,
                              tmp->ptr_info.dropped_info.line,
                              tmp->ptr_info.dropped_info.offset);
            ptr_info_mkdropped(&real->ptr_info);
            debug_ptr_info(&real->ptr_info.dropped_info, NULL);
        }
        if (tmp->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)) {
            if (real->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)) {
                /* Check the real is set again. */
                if (tmp->ptr_info.set_info.line !=
                    real->ptr_info.set_info.line) {
                    __record_ptr_info(&real->ptr_info.set_info,
                                      tmp->ptr_info.set_info.buffer,
                                      tmp->ptr_info.set_info.line,
                                      tmp->ptr_info.set_info.offset);
                    debug_variable(tmp, "set the real again (diff line)");
                    debug_ptr_info(&real->ptr_info.set_info, NULL);
                } else if (tmp->ptr_info.set_info.offset !=
                           real->ptr_info.set_info.offset) {
                    real->ptr_info.set_info.offset =
                        tmp->ptr_info.set_info.offset;
                    debug_variable(
                        tmp, "set the real again (same line, diff offset)");
                    debug_ptr_info(&real->ptr_info.set_info, NULL);
                } else {
                    // TODO: fixme
                    // TODO: should we store the ptr info as stack?
                    // we can show the warning like:
                    // the object might be released at following ...
                    //pr_debug(
                    //    "both are set, but the line/offset have problem\n");
                    //debug_ptr_info(&real->ptr_info.set_info, NULL);
                    //debug_ptr_info(&real->ptr_info.set_info, NULL);
                }
            } else if (real->ptr_info.flags & PTR_INFO_DROPPED) {
                __record_ptr_info(
                    &real->ptr_info.set_info, tmp->ptr_info.set_info.buffer,
                    tmp->ptr_info.set_info.line, tmp->ptr_info.set_info.offset);
                ptr_info_mkset(&real->ptr_info);
                debug_variable(tmp, "set the dropped var");
                debug_ptr_info(&real->ptr_info.set_info, NULL);
            }
        }
    } else {
        WARN_ON(1, "not the same variable");
        debug_variable(tmp, "tmp");
        debug_variable(real, "real");
    }
}

static void join_single_function_state(struct function *func,
                                       struct function_state *state)
{
    struct scope *scope = NULL;
    struct scope *tmp_scope = NULL;
    struct variable *tmp_var = NULL;

    debug_function(&state->function);

    tmp_var = list_first_entry(&state->function.parameter_head, struct variable,
                               parameter_node);
    list_for_each (&func->parameter_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        debug_variable(param, "join param");
        debug_variable(tmp_var, "join tmp_var");
        join_variable(param, tmp_var);
        tmp_var = list_next_entry(tmp_var, parameter_node);
    }

    tmp_scope = list_first_entry(&state->function.func_scope_head, struct scope,
                                 func_scope_node);
    list_for_each_entry (scope, &func->func_scope_head, func_scope_node) {
        struct variable *var = NULL;

        tmp_var = list_first_entry(&scope->scope_var_head, struct variable,
                                   scope_node);
        for_each_var (scope, var) {
            debug_variable(var, "join var");
            debug_variable(tmp_var, "join tmp_var");
            join_variable(var, tmp_var);
            tmp_var = list_next_entry(tmp_var, scope_node);
        }
        tmp_scope = list_next_entry(tmp_scope, func_scope_node);
    }
}

void join_function_state(struct scan_file_control *sfc)
{
    pr_debug("Join the function state start\n");

    struct function *func = sfc->function;
    list_for_each_safe (&func->state_head) {
        struct function_state *tmp =
            container_of(curr, struct function_state, state_node);
        BUG_ON(!cmp_object(&tmp->function.object, &func->object),
               "not the same function");
        list_del(&tmp->state_node);
        pr_debug("The start of join the new state\n");
        join_single_function_state(func, tmp);
        pr_debug("The end of join the new state\n");
        func->nr_state--;
    }

    pr_debug("Join the function state end\n");
}
