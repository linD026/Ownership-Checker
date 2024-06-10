#include <osc/parser.h>
#include <osc/fork.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// TODO: support union

static struct structure *compose_structure(struct scan_file_control *sfc,
                                           struct object *obj, int sym,
                                           struct symbol *symbol);

/*
 * Check function:
 *
 *      type ptr id left_paren expr right_paren
 *      left_brace
 *
 *          * id = expr
 *
 *          return id
 *      right_brace
 *
 * The routine will be:
 *      1. token = get_token()
 *      2. check token type then select next path, if:
 *          - is type, check pointer, check id, goto 3
 *          - is id, check write opration, function call, goto 4
 *      3. if left_paren and is file scope, check paramter
 *         until read right_paren, and check scope
 *      4. check all the token's attr.
 */

static void __new_scope(struct scan_file_control *sfc)
{
    // cache the toppest scope
    struct scope *scope = malloc(sizeof(struct scope));
    BUG_ON(!scope, "malloc");

    list_init(&scope->scope_var_head);
    list_init(&scope->func_scope_node);

    list_add(&scope->func_scope_node, &sfc->function->func_scope_head);
}

#define new_scope(sfc)           \
    do {                         \
        __new_scope(sfc);        \
        pr_debug("new scope\n"); \
    } while (0)

static struct scope *get_current_scope(struct scan_file_control *sfc)
{
    struct list_head *first = NULL;
    struct scope *scope = NULL;

    if (list_empty(&sfc->function->func_scope_head))
        return NULL;

    first = sfc->function->func_scope_head.next;
    scope = container_of(first, struct scope, func_scope_node);

    return scope;
}

static void insert_var_scope(struct scan_file_control *sfc,
                             struct variable *var)
{
    struct scope *scope = get_current_scope(sfc);

    BUG_ON(!scope, "scope doesn't existed");
    list_add_tail(&var->scope_node, &scope->scope_var_head);
}

static int __put_current_scope(struct scan_file_control *sfc)
{
    struct scope *scope = NULL;
    struct variable *var = NULL;

    scope = get_current_scope(sfc);
    if (!scope)
        return 1;
    for_each_var (scope, var) {
        if (check_ownership_dropped(sfc, &var->object)) {
            list_del(&scope->func_scope_node);
            return -1;
        }
    }
    list_del(&scope->func_scope_node);

    return 0;
}

#define put_current_scope(sfc)    \
    do {                          \
        pr_debug("put scope\n");  \
        __put_current_scope(sfc); \
    } while (0)

/*
 * The object type is:
 * - type __attribute__ ptr id
 */
static int compose_object(struct scan_file_control *sfc, struct object *obj,
                          int sym, struct symbol *symbol)
{
    object_init(obj);

    /* variable declaration */
    if (range_in_sym(storage_class, sym)) {
        obj->storage_class = sym;
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    if (range_in_sym(qualifier, sym)) {
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    /* the typedef case */
    if (sym == sym_id && 0) {
        // search orig type

        // struct typedef_info *ti = search_typedef_info(sfc, obj->id);
        // assigned to the old type

        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    if (range_in_sym(type, sym)) {
        obj->type = sym;
        if (sym == sym_struct) {
            /*
             * structure
             *
             * There are two type of declarations:
             *
             *   1. struct struct_id { } [id];
             *   2. struct { } ...;
             *
             * The function return type:
             *
             *   3. struct struct_id * function() { }
             *
             * And, the variable declaration:
             *
             *   4. struct struct_id * id = ... ;
             */
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (sym == sym_id) {
                /* type 1, 3, 4 */
                obj->struct_id = symbol;
                sym = get_token(sfc, &symbol);
                debug_token(sfc, sym, symbol);
            } else
                /* type 2 */
                obj->struct_id = new_anon_symbol();

            if (sym == sym_left_brace) {
                /* type 1 */
                compose_structure(sfc, obj, sym, symbol);
                return sym_struct;
            }

            /* type 3, 4 */
            goto attr_again;
        }
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    if (range_in_sym(qualifier, sym)) {
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

attr_again:
    if (range_in_sym(attr, sym)) {
        obj->attr |= get_attr_flag(sym);
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        goto attr_again;
    }

    if (range_in_sym(qualifier, sym)) {
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    if (sym == sym_aster) {
        // TODO: change the is_ptr to counter instead of flag
        obj->is_ptr = 1;
        do {
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
        } while (sym == sym_aster);
    }

    if (range_in_sym(qualifier, sym)) {
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }

    if (sym == sym_id) {
        // assert the sym_id is not the typedef symbol
        obj->id = symbol;
    }

    return sym;
}

static int get_object(struct scan_file_control *sfc, struct object *obj)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    sym = get_token(sfc, &symbol);
    if (sym == -ENODATA)
        return -ENODATA;
    debug_token(sfc, sym, symbol);

    return compose_object(sfc, obj, sym, symbol);
}

static void copy_structure(struct structure *dst, struct structure *src)
{
    copy_object(&dst->object, &src->object);

    list_for_each (&src->struct_head) {
        struct variable *src_mem =
            container_of(curr, struct variable, struct_node);
        struct variable *dst_var = var_alloc();

        if (src_mem->object.type == sym_struct)
            copy_structure(&dst_var->struct_info, &src_mem->struct_info);
        else
            copy_object(&dst_var->object, &src_mem->object);
        list_add_tail(&dst_var->struct_node, &dst->struct_head);
    }
}

/* @obj should be the id */
static struct structure *search_structure(struct scan_file_control *sfc,
                                          struct object *obj)
{
    list_for_each (&sfc->fi->struct_head) {
        struct structure *tmp = container_of(curr, struct structure, node);
        if (cmp_token(obj->struct_id, tmp->object.struct_id))
            return tmp;
    }

    bad(sfc, "undefined structure type");
    return NULL;
}

/*
 * For the structure, we use @variable as member isntead of using
 * @object, so that we can easly create the new variable by duplicating
 * the struct type.
 *
 * TYPE: structure
 * VAR: variable
 *  => Determine the type => create the var object
 */
static struct structure *compose_structure(struct scan_file_control *sfc,
                                           struct object *obj, int sym,
                                           struct symbol *symbol)
{
    struct variable *mem = NULL;
    struct structure *s = malloc(sizeof(struct structure));
    BUG_ON(!s, "malloc");

    copy_object(&s->object, obj);
    list_init(&s->struct_head);
    // TODO: insert to the scope meta data (or internal struct),
    list_add_tail(&s->node, &sfc->fi->struct_head);

    // TODO: what about the type like:
    //
    // struct ptr_info {
    //     unsigned int flags;
    //     struct ptr_info_internal dropped_info;
    //     struct ptr_info_internal set_info;
    // };
    //
    // right now, it will generate to the following:
    //
    // struct ptr_info {
    //     unsigned int flags;
    //     struct ptr_info_internal {
    //     } dropped_info;
    //     struct ptr_info_internal {
    //     } set_info;
    // };
    //
    // But we should also hold the internal-structure infor.

    // get the token to create the structure
    // init all the member as unused state
again:
    mem = var_alloc();
    sym = get_object(sfc, &mem->object);
    if (sym != sym_right_brace) {
        WARN_ON(sym != sym_id && sym != sym_struct, "unexpect symbol:%d", sym);
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        if (sym == sym_id) {
            if (mem->object.type == sym_struct) {
                struct structure *tmp = search_structure(sfc, &mem->object);
                BUG_ON(!tmp, "not found the structure");
                copy_structure(&mem->struct_info, tmp);
            }
            /*
             * copy_structure() will clean the mem->object.id,
             * so write the id here.
             */
            mem->object.id = symbol;
            debug_object(&mem->object, "the structure member");
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
        }
        if (sym == sym_seq_point) {
            list_add_tail(&mem->struct_node, &s->struct_head);
            goto again;
        }
    }

    /*
     * Two types to close the struct:
     *
     *   - struct { } ;
     *   - struct { } id ;
     */
    if (WARN_ON(sym != sym_right_brace && sym != sym_id,
                "unexpect close structure"))
        syntax_error(sfc);

    return s;
}

static void set_struct_member(struct scan_file_control *sfc,
                              struct structure *s, struct object *obj)
{
    list_for_each (&s->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);

        if (cmp_token(mem->object.id, obj->id)) {
            set_variable(sfc, mem);
            return;
        }
    }

    bad(sfc, "undefined structure member");
}

static void drop_struct_member(struct scan_file_control *sfc,
                               struct structure *s, struct object *obj)
{
    list_for_each (&s->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);

        if (cmp_token(mem->object.id, obj->id)) {
            drop_variable(sfc, mem);
            return;
        }
    }

    bad(sfc, "undefined structure member");
    debug_structure(s, "drop struct member");
}

/* function scope related functions */

static struct variable *search_var_in_function(struct function *func,
                                               struct symbol *symbol)
{
    struct scope_iter_data iter;

    list_for_each (&func->parameter_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        if (cmp_token(param->object.id, symbol))
            return param;
    }

    for_each_var_in_scopes (func, &iter) {
        struct variable *var = iter.var;
        if (cmp_token(var->object.id, symbol))
            return var;
    }
    return NULL;
}

static int decode_variable(struct scan_file_control *sfc, int *ret_sym,
                           struct symbol **ret_symbol, struct symbol *id,
                           bool set)
{
    struct symbol *symbol = *ret_symbol;
    int sym = *ret_sym;
    int ret = 0;
    struct variable *var = search_var_in_function(sfc->function, id);

    if (unlikely(!var)) {
        bad(sfc, "unkown symbol");
        ret = -EINVAL;
        goto out;
    }

    if (var->object.type == sym_struct) {
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        if (sym == sym_dot || sym == sym_ptr_assign) {
            struct object tmp_obj;
            sym = get_object(sfc, &tmp_obj);
            if (sym == sym_id) {
                if (set) {
                    debug_object(&tmp_obj, "set struct member");
                    set_struct_member(sfc, &var->struct_info, &tmp_obj);
                } else {
                    debug_object(&tmp_obj, "drop struct member");
                    drop_struct_member(sfc, &var->struct_info, &tmp_obj);
                }
            }
        } else {
            ret = -EAGAIN;
            goto out;
        }
    } else if (var->object.attr & ATTR_FLAGS_MUT) {
        /* We only check the mut attribute */
        if (set) {
            debug_object(&var->object, "set the var");
            set_variable(sfc, var);
        } else {
            debug_object(&var->object, "drop the var");
            drop_variable(sfc, var);
        }
    }

out:
    *ret_sym = sym;
    *ret_symbol = symbol;

    return ret;
}

/*
 * With typedef, the checker should first get the original type so we should
 * mark the variable with the typedef type, therefore, the checker can get
 * the corresponding type. In this case, we can reuse the sym_typedef as
 * new type flag in object. For example. object->type = sym_typedef.
 *
 * So the routine will be like:
 *  1. get new type from typedef.
 *  2. get the object with typdef type.
 *  3. parse the object and send it to the checker
 *  4. in the checker, find the original type first and check it
 */
static int decode_typedef(struct scan_file_control *sfc)
{
    int sym = sym_dump;
    struct object tmp_obj;
    struct typedef_info_node *tin, *pos;
    struct typedef_info *ti = NULL;

    tin = malloc(sizeof(struct typedef_info_node));
    BUG_ON(!tin, "malloc");

    /*
     * the pattern is:
     *      typedef TYPE __ATTRIBUTE__ NEW_TYPE
     *      - typedef struct { ... } NEW_TYPE
     *      - typedef struct TYPE { ... } NEW_TYPE
     *      - typedef TYPE (...NEW_TYPE)( ... )
     * we already got the typedef symbol, so we can use the
     * get_object() to compose the rest of symbols.
     */
    // TODO: support function pointer type
    sym = get_object(sfc, &tmp_obj);
    debug_object(&tmp_obj, "typedef tmp object");

    /*
     * Now, we have all the info in tmp_obj.
     * The tmp_obj->id is the new type.
     */

    tin->new_type_symbol = tmp_obj.id;

    // find the original type from the typede_info data.
    // If we have it, insert the new type symbol into its list
    // store the original type into info node

    // current_scope->typedef_info_head...
    list_for_each (&sfc->typedef_info_head) {
        ti = container_of(curr, struct typedef_info, node);

        // we only care about the ptr, attr, and type info.
        // TODO: we should assign the constant debug token to this.
        tmp_obj.id = ti->orig_object.id;
        if (cmp_object(&ti->orig_object, &tmp_obj)) {
            goto insert_tin;
        }
    }

    ti = malloc(sizeof(struct typedef_info));
    list_init(&ti->head);
    list_init(&ti->node);
    copy_object(&ti->orig_object, &tmp_obj);

    // insert and return back

insert_tin:
    list_for_each_entry (pos, &ti->head, node) {
        if (cmp_token(pos->new_type_symbol, tin->new_type_symbol)) {
            // already have the symbol
            bad(sfc, "the symbol already existed");
        }
    }

    // insert

    // create the type info to token/object
    // mark the new_type symbol?

    // anon structure?

    return sym;
}

static int decode_func_call(struct scan_file_control *sfc,
                            struct symbol *func_symbol)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        debug_token(sfc, sym, symbol);
    again:
        if (sym == sym_comma)
            continue;
        if (sym == sym_right_paren)
            return sym;

        if (sym == sym_id) {
            if (decode_variable(sfc, &sym, &symbol, symbol, false) == -EAGAIN)
                goto again;
        }
    }

    return sym;
}

/*
 * Before we call to this function, we need to make sure that
 * the current function return type is pointer.
 */
static int decode_func_return(struct scan_file_control *sfc)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        debug_token(sfc, sym, symbol);
        // TODO: address of
        if (sym == sym_logic_and)
            continue;
        if (sym == sym_id) {
            struct object tmp_obj;
            sym = compose_object(sfc, &tmp_obj, sym, symbol);
            if (sym == sym_struct) {
                struct structure __allow_unused *tmp =
                    search_structure(sfc, &tmp_obj);

                // TODO: recheck the logic
                sym = get_token(sfc, &symbol);
                debug_token(sfc, sym, symbol);
                if (sym == sym_dot || sym == sym_ptr_assign) {
                    struct object tmp_mem_obj;
                    sym = compose_object(sfc, &tmp_mem_obj, sym, symbol);
                    debug_object(&tmp_mem_obj, "struct member");
                }
            }
            check_ownership_owned(sfc, &tmp_obj);
            return sym;
        }
        if (sym == sym_seq_point)
            return sym;
    }

    return sym;
}

static int decode_stmt(struct scan_file_control *sfc, struct symbol *symbol,
                       int sym);
static int decode_expr(struct scan_file_control *sfc, struct symbol *symbol,
                       int sym);
static int decode_function_scope(struct scan_file_control *sfc);
static int decode_new_block(struct scan_file_control *sfc, int sym,
                            struct symbol *symbol);

static int decode_if(struct scan_file_control *sfc, struct symbol *symbol,
                     int sym)
{
    pr_debug("if statement start\n");
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (unlikely(sym != sym_left_paren))
        syntax_error(sfc);
    sym = decode_expr(sfc, symbol, sym);
    if (unlikely(sym != sym_right_paren))
        syntax_error(sfc);
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (sym == sym_left_brace) {
        fork_and_switch_function_state(sfc);
        new_scope(sfc);
        sym = decode_new_block(sfc, sym, symbol);
    } else {
        sym = decode_stmt(sfc, symbol, sym);
    }

peak_else:
    sym = peak_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);

    if (sym == sym_else) {
        /* flush the peak token */
        flush_peak_token(sfc);

        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        if (sym == sym_if) {
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (unlikely(sym != sym_left_paren))
                syntax_error(sfc);
            sym = decode_expr(sfc, symbol, sym);
            if (unlikely(sym != sym_right_paren))
                syntax_error(sfc);
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (sym == sym_left_brace) {
                fork_and_switch_function_state(sfc);
                new_scope(sfc);
                sym = decode_new_block(sfc, sym, symbol);
            } else {
                sym = decode_stmt(sfc, symbol, sym);
            }
            goto peak_else;
        }

        fork_and_switch_function_state(sfc);

        if (sym == sym_left_brace) {
            new_scope(sfc);
            sym = decode_new_block(sfc, sym, symbol);
        } else {
            sym = decode_stmt(sfc, symbol, sym);
        }
    }

    restore_function_state(sfc);
    join_function_state(sfc);
    pr_debug("if statement end(sym=%d)\n", sym);

    return sym;
}

static int decode_do_while_loop(struct scan_file_control *sfc,
                                struct symbol *symbol, int sym)
{
    pr_debug("do while loop start\n");

    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (sym != sym_left_brace)
        syntax_error(sfc);

    new_scope(sfc);
    sym = decode_new_block(sfc, sym, symbol);
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    BUG_ON(sym != sym_while, "do while loop");

    // TODO: handle the do-while expr
    pr_debug("do while loop end\n");

    return sym;
}

static int decode_while_loop(struct scan_file_control *sfc,
                             struct symbol *symbol, int sym)
{
    pr_debug("while loop start\n");
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (sym != sym_left_paren)
        syntax_error(sfc);

    // TODO: Should we check the lifetime?
    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        struct object tmp_obj;

    again:
        debug_token(sfc, sym, symbol);
        sym = compose_object(sfc, &tmp_obj, sym, symbol);
        if (sym == sym_id) {
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (sym == sym_eq) {
                /* assignment */
                debug_object(&tmp_obj, "be wrote");
                /* See the comments in decode_stmt()'s assignment part. */
                if (!tmp_obj.is_ptr) {
                    if (decode_variable(sfc, &sym, &symbol, tmp_obj.id, true) ==
                        -EAGAIN)
                        goto again;
                }
                check_ownership_writable(sfc, &tmp_obj);
                sym = decode_expr(sfc, symbol, sym);
            }
            if (sym == sym_left_paren) {
                /* function call */
                sym = decode_func_call(sfc, symbol);
            }
        }
        if (sym == sym_right_paren)
            break;
    }
    if (sym != sym_right_paren)
        syntax_error(sfc);

    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (sym == sym_left_brace) {
        new_scope(sfc);
        sym = decode_new_block(sfc, sym, symbol);
        BUG_ON(sym != sym_right_brace, "while loop");
    }

    pr_debug("while loop end\n");

    return sym;
}

static int decode_for_loop(struct scan_file_control *sfc, struct symbol *symbol,
                           int sym)
{
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);

    pr_debug("for loop start\n");
    if (sym != sym_left_paren)
        syntax_error(sfc);
    new_scope(sfc);
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    sym = decode_stmt(sfc, symbol, sym);
    if (sym != sym_seq_point)
        syntax_error(sfc);
    pr_debug("for loop first statement\n");
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    sym = decode_expr(sfc, symbol, sym);
    if (sym != sym_seq_point)
        syntax_error(sfc);
    pr_debug("for loop second statement\n");
    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        struct object tmp_obj;

    again:
        debug_token(sfc, sym, symbol);
        sym = compose_object(sfc, &tmp_obj, sym, symbol);
        if (sym == sym_id) {
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (sym == sym_eq) {
                /* assignment */
                debug_object(&tmp_obj, "be wrote");
                /* See the comments in decode_stmt()'s assignment part. */
                if (!tmp_obj.is_ptr) {
                    if (decode_variable(sfc, &sym, &symbol, tmp_obj.id, true) ==
                        -EAGAIN)
                        goto again;
                }
                check_ownership_writable(sfc, &tmp_obj);
                sym = decode_expr(sfc, symbol, sym);
            }
            if (sym == sym_left_paren) {
                /* function call */
                sym = decode_func_call(sfc, symbol);
            }
        }
        if (sym == sym_right_paren)
            break;
    }
    if (sym != sym_right_paren)
        syntax_error(sfc);
    pr_debug("for loop third statement\n");

    sym = get_token(sfc, &symbol);
    debug_token(sfc, sym, symbol);
    if (sym == sym_left_brace) {
        new_scope(sfc);
        sym = decode_new_block(sfc, sym, symbol);
        BUG_ON(sym != sym_right_brace, "for loop");
    } else
        sym = decode_expr(sfc, symbol, sym);
    put_current_scope(sfc);

    return sym;
}

static int decode_expr(struct scan_file_control *sfc, struct symbol *symbol,
                       int sym)
{
    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        struct object tmp_obj;

    again:
        debug_token(sfc, sym, symbol);
        sym = compose_object(sfc, &tmp_obj, sym, symbol);
        if (sym == sym_id) {
            sym = get_token(sfc, &symbol);
            debug_token(sfc, sym, symbol);
            if (sym == sym_eq) {
                /* assignment */
                debug_object(&tmp_obj, "be wrote");
                /* See the comments in decode_stmt()'s assignment part. */
                if (!tmp_obj.is_ptr) {
                    if (decode_variable(sfc, &sym, &symbol, tmp_obj.id, true) ==
                        -EAGAIN)
                        goto again;
                }
                check_ownership_writable(sfc, &tmp_obj);
                sym = decode_expr(sfc, symbol, sym);
            }
            if (sym == sym_left_paren) {
                /* function call */
                sym = decode_func_call(sfc, symbol);
            }
        }
        if (sym == sym_return) {
            if (sfc->function->object.is_ptr) {
                sym = decode_func_return(sfc);
            }
        }
        if (sym == sym_seq_point || sym == sym_right_paren)
            return sym;
    }

    return sym;
}

static int decode_stmt(struct scan_file_control *sfc, struct symbol *symbol,
                       int sym)
{
    do {
        struct object tmp_obj;

    again:
        debug_token(sfc, sym, symbol);
        sym = compose_object(sfc, &tmp_obj, sym, symbol);
        if (sym == sym_id || sym == sym_struct) {
            struct symbol *orig_symbol = symbol;

            /* variable declaration */
            if (range_in_sym(storage_class, tmp_obj.storage_class) ||
                range_in_sym(type, tmp_obj.type)) {
                struct variable *var = var_alloc();

                if (tmp_obj.type == sym_struct) {
                    struct structure *tmp_s = search_structure(sfc, &tmp_obj);
                    copy_structure(&var->struct_info, tmp_s);
                    /*
                     * copy_structure only copy the struct info,
                     * we should use copy_object to copy the var info (ie, id).
                     */
                    if (tmp_obj.id) {
                        copy_object(&var->object, &tmp_obj);
                        debug_structure(&var->struct_info,
                                        "declare struct var in scope");
                    } else
                        debug_structure(&var->struct_info,
                                        "declare struct type in scope");
                } else {
                    copy_object(&var->object, &tmp_obj);
                    debug_object(&var->object, "declare var in scope");
                }
                if (tmp_obj.id)
                    insert_var_scope(sfc, var);
            }

            sym = get_token(sfc, &symbol);
            if (sym == sym_eq) {
                /* assignment */
                debug_token(sfc, sym, symbol);
                debug_object(&tmp_obj, "be wrote");
                /*
                 * Two types:
                 *
                 * The object is from var declaration, we should check it
                 *   - type *id = ... ;
                 * The object is not from var declaration, and doesn't not
                 * have ptr type.
                 *   - ptr_id = ... ;
                 */
                if (range_in_sym(type, tmp_obj.type) || !tmp_obj.is_ptr) {
                    if (decode_variable(sfc, &sym, &symbol, tmp_obj.id, true) ==
                        -EAGAIN)
                        goto again;
                }
                check_ownership_writable(sfc, &tmp_obj);
                sym = decode_expr(sfc, symbol, sym);
            } else if (sym == sym_left_paren) {
                /* function call start */
                debug_object(&tmp_obj, "function call start");
                sym = decode_func_call(sfc, orig_symbol);
            } else {
                debug_object(&tmp_obj, "decalaration only");
            }
        } else if (sym == sym_typedef) {
            sym = decode_typedef(sfc);
            continue;
        } else if (sym == sym_if) {
            sym = decode_if(sfc, symbol, sym);
            // TODO: how to handle the peak?
            continue;
        } else if (sym == sym_return) {
            if (sfc->function->object.is_ptr) {
                sym = decode_func_return(sfc);
            }
        } else if (sym == sym_do) {
            sym = decode_do_while_loop(sfc, symbol, sym);
        } else if (sym == sym_while) {
            sym = decode_while_loop(sfc, symbol, sym);
            continue;
        } else if (sym == sym_for) {
            sym = decode_for_loop(sfc, symbol, sym);
            continue;
        }

        if (sym == sym_left_brace) {
            new_scope(sfc);
            sym = decode_new_block(sfc, sym, symbol);
            continue;
        }

        /*
         * The if, while-loop, for-loop statements have their own scope
         * (i.e., the brace pair) and their right brace might be the last
         * of the token in the function. In this case, we don't peak
         * the next token in those decoders instead we return back here
         * and skip the following checking.
         */
        if (sym == sym_seq_point || sym == sym_right_brace)
            return sym;
    } while (sym = get_token(sfc, &symbol), sym != -ENODATA);

    return sym;
}

static int decode_new_block(struct scan_file_control *sfc, int sym,
                            struct symbol *symbol)
{
    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        debug_token(sfc, sym, symbol);
        if (sym == sym_right_brace) {
        exit:
            /*
             * We already decoded the sym_left_brace in decode_file_scope(),
             * so we just put the current scope and return back to upper stack
             * (decode_file_scope() or decode_function_scope()).
             */
            put_current_scope(sfc);
            return sym;
        } else if (sym == sym_left_brace) {
            new_scope(sfc);
            /*
             * The recursive function call should be after sym_right_brace,
             * Otherwise, we cannot get the brace pairs correctly.
             */
            sym = decode_new_block(sfc, sym, symbol);
        } else {
            sym = decode_stmt(sfc, symbol, sym);
            if (sym == sym_right_brace)
                goto exit;
            WARN_ON(sym != sym_seq_point, "decode_stmt:%c, sym=%d",
                    debug_sym_one_char(sym), sym);
        }
    };

    return sym;
}

static int decode_function_scope(struct scan_file_control *sfc)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;
    return decode_new_block(sfc, sym, symbol);
}

/* file scope related functions */

static struct function *search_function(struct file_info *fi,
                                        struct object *obj)
{
    list_for_each (&fi->func_head) {
        struct function *func = container_of(curr, struct function, node);
        if (cmp_object(&func->object, obj))
            return func;
    }
    return NULL;
}

static struct function *insert_function(struct file_info *fi,
                                        struct object *obj)
{
    struct function *func = search_function(fi, obj);

    if (func) {
        BUG_ON(!list_empty(&func->func_scope_head),
               "Duplicate function definition");
        return func;
    }
    func = malloc(sizeof(struct function));
    BUG_ON(!func, "malloc");

    list_init(&func->func_scope_head);
    copy_object(&func->object, obj);
    list_init(&func->parameter_head);
    func->nr_state = 0;
    list_init(&func->state_head);
    list_add_tail(&func->node, &fi->func_head);

    return func;
}

static int decode_file_scope(struct scan_file_control *sfc)
{
    struct object obj;
    struct symbol *buffer = NULL;
    int sym = sym_dump;

again:
    /*
     * Get the object like:
     * - struct struture
     * - int __attr *function
     */
    sym = get_object(sfc, &obj);
    if (sym == -ENODATA)
        return -ENODATA;

        // TODO: global var?

        /*
     * If the sym is sym_struct, this means that the function
     * might be return type is struct.
     */

#ifdef CONFIG_DEBUG
    debug_object(&obj, "global object");
    if (sym == sym_struct) {
        struct structure *tmp = search_structure(sfc, &obj);
        BUG_ON(!tmp, "we should search the structure successfully");
        debug_structure(tmp, "global structure");
    }
#endif

    /*
     * Skip the seq_point symbol.
     * We have to handle this outside of compose functions.
     */
    sym = get_token(sfc, &buffer);
    debug_token(sfc, sym, buffer);
    if (sym == sym_seq_point)
        goto again;

    /* Function */
    sfc->function = insert_function(sfc->fi, &obj);
    sfc->real_function = sfc->function;

    if (sym == sym_left_paren) {
        /* parse the function paramters */
        while (1) {
            struct variable *param = var_alloc();

            sym = get_object(sfc, &param->object);
            /* non-parameter type of function: func(void) */
            if (unlikely(sym != sym_id && param->object.type == sym_void)) {
                free(param);
                break;
            } else {
                list_add_tail(&param->parameter_node,
                              &sfc->function->parameter_head);
                param->ptr_info.flags |= PTR_INFO_FUNC_ARG;
            }
            sym = get_token(sfc, &buffer);
            if (sym != sym_comma)
                break;
        }
        /* We are the end of parameter list, check if the sym is ")" or not. */
        if (sym != sym_right_paren) {
            WARN_ON(1, "syntax error %c", debug_sym_one_char(sym));
            syntax_error(sfc);
        }

        sym = get_token(sfc, &buffer);
        /* function declaration */
        if (sym == sym_seq_point) {
            debug_function(sfc->function);
            goto out;
        } else if (sym == sym_left_brace) {
            /* function definition */
            debug_function(sfc->function);
            new_scope(sfc);
            sym = decode_function_scope(sfc);
            WARN_ON(sym != sym_right_brace, "decode_function_scope:%c, sym=%d",
                    debug_sym_one_char(sym), sym);
        } else {
            WARN_ON(1, "syntax error");
            syntax_error(sfc);
        }
    } else {
        WARN_ON(1, "syntax error");
        syntax_error(sfc);
    }

out:
    sfc->function = NULL;
    return 0;
}

static void scan_file(struct scan_file_control *sfc)
{
    token_init(sfc);
    while (decode_file_scope(sfc) != -ENODATA)
        ;
}

int parser(struct file_info *fi)
{
    struct scan_file_control sfc = {
        .fi = fi,
        .size = MAX_BUFFER_LEN,
        .offset = 0,
        .line = 0,
        .peak = 0,
        .function = NULL,
    };

    /*
     * In this case, we have three names for the files,
     * one name for the error message.
     *
     * - original file name: fi->name
     * - original file name with full path: fi->full_name
     * - generated file name by compiler (original file name with -P flag):
     *   fi->genertad_name
     * - the name show on error message (this should be same as fi->name):
     *   sfc->name
     */
    list_init(&sfc.peak_head);
    strncpy(sfc.name, fi->generated_name, MAX_NR_GENERATED_NAME);
    fi->file = fopen(fi->generated_name, "r");
    BUG_ON(!fi->file, "fopen:%s", fi->generated_name);
    rewind(fi->file);
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
