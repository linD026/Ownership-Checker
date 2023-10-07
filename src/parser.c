#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static struct function_state *fork_function_state(struct function *func);
static void switch_function_state(struct scan_file_control *sfc,
                                  struct function_state *new);
static void fork_and_switch_function_state(struct scan_file_control *sfc);
static void restore_function_state(struct scan_file_control *sfc);
static void join_function_state(struct scan_file_control *sfc);
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

static void object_init(struct object *object)
{
    object->storage_class = sym_dump;
    object->type = sym_dump;
    object->is_ptr = 0;
    object->attr = 0;
    object->struct_id = NULL;
    object->id = NULL;
}

static int cmp_object(struct object *l, struct object *r)
{
    if (l->storage_class != r->storage_class)
        return 0;
    if (l->type != r->type)
        return 0;
    if (l->is_ptr != r->is_ptr)
        return 0;
    if (l->attr != r->attr)
        return 0;
    // TODO: We should improve this condition checkings...
    if (l->struct_id && r->struct_id) {
        if (!cmp_token(l->struct_id, r->struct_id))
            return 0;
    } else if ((l->struct_id && !r->struct_id) ||
               (!l->struct_id && r->struct_id))
        return 0;
    if (!cmp_token(l->id, r->id))
        return 0;
    return 1;
}

static void copy_object(struct object *dst, struct object *src)
{
    *dst = *src;
}

static int get_attr_flag(int sym)
{
    switch (sym) {
    case sym_attr_brw:
        return ATTR_FLAGS_BRW;
    case sym_attr_clone:
        return ATTR_FLAGS_CLONE;
    case sym_attr_mut:
        return ATTR_FLAGS_MUT;
    }
    return 0;
}

static __allow_unused void raw_debug_object(struct object *obj)
{
#ifdef CONFIG_DEBUG
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
#endif /* CONFIG_DEBUG */
}

static void debug_object(struct object *obj, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[OBJECT] %s: ", note);
    raw_debug_object(obj);
    print(" \n");
#endif /* CONFIG_DEBUG */
}

/*
 * The object type is:
 * - type __attribute__ ptr id
 */
static int compose_object(struct scan_file_control *sfc, struct object *obj,
                          int sym, struct symbol *symbol)
{
again:
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

    if (sym == sym_id)
        obj->id = symbol;

    if (sym == sym_include || sym == sym_define) {
        next_line(sfc);
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        goto again;
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

static void __record_ptr_info(struct ptr_info_internal *info,
                              const char *buffer, unsigned long line,
                              unsigned int offset)
{
    strncpy(info->buffer, buffer, MAX_BUFFER_LEN);
    info->line = line;
    /*
     * We adapt the offset to the last symbol when we report the warning.
     * See the bad_get_last_offset();
     */
    info->offset = offset;
}

static void record_ptr_info(struct scan_file_control *sfc,
                            struct ptr_info_internal *info)
{
    __record_ptr_info(info, sfc->buffer, sfc->line, sfc->offset);
}

static void ptr_info_mkset(struct ptr_info *info)
{
    info->flags &= ~PTR_INFO_DROPPED;
    info->flags |= PTR_INFO_SET;
}

static void ptr_info_mkdropped(struct ptr_info *info)
{
    info->flags |= PTR_INFO_DROPPED;
}

static void drop_variable(struct scan_file_control *sfc, struct variable *var)
{
    // TODO: don't just warn it
    WARN_ON(!(var->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)),
            "drop the unassigned ptr");
    record_ptr_info(sfc, &var->ptr_info.dropped_info);
    ptr_info_mkdropped(&var->ptr_info);
    debug_ptr_info(&var->ptr_info.dropped_info, NULL);
}

static void set_variable(struct scan_file_control *sfc, struct variable *var)
{
#ifdef CONFIG_DEBUG
    if (var->ptr_info.flags & PTR_INFO_DROPPED) {
        pr_debug("variable %s; re-assigned after dropped\n",
                 var->object.id->name);
    }
#endif
    record_ptr_info(sfc, &var->ptr_info.set_info);
    ptr_info_mkset(&var->ptr_info);
    debug_ptr_info(&var->ptr_info.set_info, NULL);
}

static struct variable *var_alloc(void)
{
    struct variable *var = malloc(sizeof(struct variable));
    BUG_ON(!var, "malloc");

    var->ptr_info.flags = 0;
    object_init(&var->object);
    list_init(&var->struct_info.struct_head);
    list_init(&var->struct_info.node);
    list_init(&var->scope_node);
    list_init(&var->struct_node);
    list_init(&var->parameter_node);

    return var;
}

static void copy_variable(struct variable *dst, struct variable *src)
{
    dst->ptr_info.flags = src->ptr_info.flags;
    if (dst->ptr_info.flags & PTR_INFO_SET) {
        __record_ptr_info(
            &dst->ptr_info.set_info, src->ptr_info.set_info.buffer,
            src->ptr_info.set_info.line, src->ptr_info.set_info.offset);
    }
    if (dst->ptr_info.flags & PTR_INFO_DROPPED) {
        __record_ptr_info(
            &dst->ptr_info.dropped_info, src->ptr_info.dropped_info.buffer,
            src->ptr_info.dropped_info.line, src->ptr_info.dropped_info.offset);
    }

    copy_object(&dst->object, &src->object);
}

static void debug_variable(struct variable *var, const char *note)
{
#ifdef CONFIG_DEBUG
    struct ptr_info *info = &var->ptr_info;
    print("[VAR] ");
    debug_object(&var->object, note);
    if (info->flags & PTR_INFO_FUNC_ARG) {
        print("[VAR] Is func parrameter\n");
    }
    if (info->flags & PTR_INFO_SET) {
        print("[VAR] set at:%ld:%u\n", info->set_info.line,
              info->set_info.offset);
    }
    if (info->flags & PTR_INFO_DROPPED) {
        print("[VAR] dropped at:%ld:%u\n", info->dropped_info.line,
              info->dropped_info.offset);
    }
#endif
}

#ifdef CONFIG_DEBUG
static void debug_space_level(int nested_level)
{
    for (int i = 0; i < nested_level; i++)
        print("    ");
}

static void raw_debug_structure(struct structure *structure, int nested_level)
{
    print("struct %s ", structure->object.struct_id->name);
    print("{\n");
    list_for_each (&structure->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);
        debug_space_level(nested_level);
        if (mem->object.type == sym_struct)
            raw_debug_structure(&mem->struct_info, nested_level + 1);
        else {
            raw_debug_object(&mem->object);
            print(";\n");
        }
    }

    if (nested_level > 1)
        debug_space_level(nested_level - 1);

    if (structure->object.id) {
        print("} %s;\n", structure->object.id->name);
    } else {
        print("};\n");
    }
}
#endif

static void debug_structure(struct structure *structure, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[STRUCT START]: %s\n", note);
    raw_debug_structure(structure, 1);
    print("[STRUCT END]\n");
#endif /* CONFIG_DEBUG */
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

static void debug_function(struct function *function)
{
#ifdef CONFIG_DEBUG
    print("[FUNC] ");
    raw_debug_object(&function->object);
    print(" (");
    if (unlikely(list_empty(&function->parameter_head)))
        print("void");
    else {
        list_for_each (&function->parameter_head) {
            struct variable *param =
                container_of(curr, struct variable, parameter_node);
            raw_debug_object(&param->object);
            if (curr->next != &function->parameter_head)
                print(", ");
        }
    }
    print(")");

    // TODO: scope object
    print("\n");
#endif /* CONFIG_DEBUG */
}

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
static struct function_state *fork_function_state(struct function *func)
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

static void switch_function_state(struct scan_file_control *sfc,
                                  struct function_state *new)
{
    sfc->function = &new->function;
}

static void fork_and_switch_function_state(struct scan_file_control *sfc)
{
    switch_function_state(sfc, fork_function_state(sfc->real_function));
}

static void restore_function_state(struct scan_file_control *sfc)
{
    sfc->function = sfc->real_function;
}

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

static void join_function_state(struct scan_file_control *sfc)
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
    if (next_line(sfc)) {
        while (decode_file_scope(sfc) != -ENODATA)
            ;
    }
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

    list_init(&sfc.peak_head);

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen:%s", fi->name);
    rewind(fi->file);
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
