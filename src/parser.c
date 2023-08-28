#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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

static void new_scope(struct scan_file_control *sfc)
{
    // cache the toppest scope
    struct scope *scope = malloc(sizeof(struct scope));
    BUG_ON(!scope, "malloc");

    list_init(&scope->scope_var_head);
    list_init(&scope->func_scope_node);

    list_add(&scope->func_scope_node, &sfc->function->func_scope_head);
    pr_debug("new scope\n");
}

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

static int put_current_scope(struct scan_file_control *sfc)
{
    struct list_head *node = NULL;
    struct scope *scope = NULL;
    struct variable *var = NULL;

    if (list_empty(&sfc->function->func_scope_head))
        return 1;
    pr_debug("put scope\n");

    node = sfc->function->func_scope_head.next;
    scope = container_of(node, struct scope, func_scope_node);
    for_each_var (scope, var) {
        if (check_ownership_dropped(sfc, &var->object)) {
            list_del(node);
            return -1;
        }
    }
    list_del(node);

    return 0;
}

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
    object_init(obj);

    /* variable declaration */
    if (range_in_sym(storage_class, sym)) {
        obj->storage_class = sym;
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
attr_again:
    if (range_in_sym(attr, sym)) {
        obj->attr |= get_attr_flag(sym);
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
        goto attr_again;
    }
    if (sym == sym_aster) {
        obj->is_ptr = 1;
        sym = get_token(sfc, &symbol);
        debug_token(sfc, sym, symbol);
    }
    if (sym == sym_id)
        obj->id = symbol;

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

static void record_ptr_info(struct scan_file_control *sfc,
                            struct ptr_info_internal *info)
{
    strncpy(info->buffer, sfc->buffer, MAX_BUFFER_LEN);
    info->line = sfc->line;
    /*
     * We adapt the offset to the last symbol when we report the warning.
     * See the bad_get_last_offset();
     */
    info->offset = sfc->offset;
}

static void drop_variable(struct scan_file_control *sfc, struct variable *var)
{
    // TODO: don't just warn it
    WARN_ON(!(var->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)),
            "drop the unassigned ptr");
    record_ptr_info(sfc, &var->ptr_info.dropped_info);
    var->ptr_info.flags |= PTR_INFO_DROPPED;
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
    var->ptr_info.flags &= ~PTR_INFO_DROPPED;
    var->ptr_info.flags |= PTR_INFO_SET;
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

    list_for_each (&func->parameter_var_head) {
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

static int decode_expr(struct scan_file_control *sfc, struct symbol *symbol,
                       int sym)
{
    struct symbol *prev_symbol = symbol;
    int prev_sym = sym;

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        debug_token(sfc, sym, symbol);
        if (sym == sym_eq) {
            /* assignment */
            if (prev_sym == sym_id) {
                struct object tmp_obj;
                sym = compose_object(sfc, &tmp_obj, prev_sym, prev_symbol);
                if (sym == sym_struct) {
                    struct structure __allow_unused *tmp =
                        search_structure(sfc, &tmp_obj);
                    //TODO: check structure member's pointer
                    pr_debug("check the structure ownership writable\n");
                }
                check_ownership_writable(sfc, &tmp_obj);
            }
            sym = decode_expr(sfc, symbol, sym);
        }
        if (sym == sym_left_paren) {
            /* function call */
            sym = decode_func_call(sfc, prev_symbol);
        }
        if (sym == sym_return) {
            if (sfc->function->object.is_ptr) {
                sym = decode_func_return(sfc);
            }
        }
        if (sym == sym_seq_point)
            return sym;
        prev_symbol = symbol;
        prev_sym = sym;
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
                if (decode_variable(sfc, &sym, &symbol, tmp_obj.id, true) ==
                    -EAGAIN)
                    goto again;
                check_ownership_writable(sfc, &tmp_obj);
                sym = decode_expr(sfc, symbol, sym);
            } else if (sym == sym_left_paren) {
                /* function call start */
                debug_object(&tmp_obj, "function call start");
                sym = decode_func_call(sfc, orig_symbol);
            } else {
                debug_object(&tmp_obj, "decalaration only");
            }
        } else if (sym == sym_return) {
            if (sfc->function->object.is_ptr) {
                sym = decode_func_return(sfc);
            }
        }

        if (sym == sym_seq_point)
            return sym;
    } while (sym = get_token(sfc, &symbol), sym != -ENODATA);

    return 0;
}

static int decode_function_scope(struct scan_file_control *sfc)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        if (sym == sym_right_brace) {
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
            sym = decode_function_scope(sfc);
        } else {
            sym = decode_stmt(sfc, symbol, sym);
            WARN_ON(sym != sym_seq_point, "decode_stmt:%c",
                    debug_sym_one_char(sym));
        }
    }
    return sym;
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
    list_init(&func->parameter_var_head);
    list_add_tail(&func->node, &fi->func_head);

    return func;
}

static void debug_function(struct scan_file_control *sfc,
                           struct function *function)
{
#ifdef CONFIG_DEBUG
    print("[FUNC] ");
    raw_debug_object(&function->object);
    print(" (");
    if (unlikely(list_empty(&function->parameter_var_head)))
        print("void");
    else {
        list_for_each (&function->parameter_var_head) {
            struct variable *param =
                container_of(curr, struct variable, parameter_node);
            raw_debug_object(&param->object);
            if (curr->next != &function->parameter_var_head)
                print(", ");
        }
    }
    print(")");

    // TODO: scope object
    print("\n");
#endif /* CONFIG_DEBUG */
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
                              &sfc->function->parameter_var_head);
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
            debug_function(sfc, sfc->function);
            goto out;
        } else if (sym == sym_left_brace) {
            /* function definition */
            debug_function(sfc, sfc->function);
            new_scope(sfc);
            sym = decode_function_scope(sfc);
            WARN_ON(sym != sym_right_brace, "decode_function_scope:%c",
                    debug_sym_one_char(sym));
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
        .function = NULL,
    };

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen");
    rewind(fi->file);
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
