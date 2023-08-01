#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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

static struct structure *search_structure(struct symbol *id)
{
    return NULL;
}

static void object_init(struct object *object)
{
    object->storage_class = sym_dump;
    object->type = sym_dump;
    object->is_struct = 0;
    object->struct_id = NULL;
    object->is_ptr = 0;
    object->attr = 0;
    object->id = NULL;
}

static int cmp_object(struct object *l, struct object *r)
{
    if (l->storage_class != r->storage_class)
        return 0;
    if (l->type != r->type)
        return 0;
    if (l->is_struct && cmp_token(l->struct_id, r->struct_id))
        return 0;
    if (l->is_ptr != r->is_ptr)
        return 0;
    if (l->attr != r->attr)
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
    if (obj->type != sym_dump)
        print("%s ", token_name(obj->type));
    if (obj->is_struct)
        print("%s ", obj->struct_id->name);
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
            /* structure */
            bad(sfc, "Unsupport structure");
            BUG_ON(1, "unsupport");
            sym = get_token(sfc, &symbol);
            if (sym == sym_id) {
                // struct structure __allow_unused *tmp =  search_structure();
                obj->struct_id = symbol;
            } else {
                syntax_error(sfc);
                return -EINVAL;
            }
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

static void drop_variable(struct scan_file_control *sfc, struct variable *var)
{
    strncpy(var->dropped_info.buffer, sfc->buffer, MAX_BUFFER_LEN);
    var->dropped_info.line = sfc->line;
    /*
     * We adapt the offset to the last symbol when we report the warning.
     * See the bad_get_last_offset();
     */
    var->dropped_info.offset = sfc->offset;
    var->is_dropped = 1;
}

static struct variable *var_alloc(void)
{
    struct variable *var = malloc(sizeof(struct variable));
    BUG_ON(!var, "malloc");

    var->is_dropped = 0;
    object_init(&var->object);
    list_init(&var->scope_node);
    list_init(&var->structure_node);
    list_init(&var->func_scope_node);
    list_init(&var->parameter_node);

    return var;
}

/* function scope related functions */

static struct variable *search_var_in_function(struct function *func,
                                               struct symbol *symbol)
{
    list_for_each (&func->parameter_var_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        if (cmp_token(param->object.id, symbol))
            return param;
    }

    list_for_each (&func->func_scope_var_head) {
        struct variable *var =
            container_of(curr, struct variable, func_scope_node);
        if (cmp_token(var->object.id, symbol))
            return var;
    }
    return NULL;
}

static int decode_func_call(struct scan_file_control *sfc,
                            struct symbol *func_symbol)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    while (sym = get_token(sfc, &symbol), sym != -ENODATA) {
        debug_token(sfc, sym, symbol);
        if (sym == sym_comma)
            continue;
        if (sym == sym_right_paren)
            return sym;

        if (sym == sym_id) {
            struct variable *var =
                search_var_in_function(sfc->function, symbol);
            if (unlikely(!var)) {
                bad(sfc, "unkown symbol");
                continue;
            }
            /* We only check the mut attribute */
            if (var->object.attr & ATTR_FLAGS_MUT) {
                debug_object(&var->object, "dropped the var");
                drop_variable(sfc, var);
            }
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
            check_ownership_owned(sfc, &tmp_obj);
            return sym;
        } else if (sym == sym_seq_point)
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

        debug_token(sfc, sym, symbol);
        sym = compose_object(sfc, &tmp_obj, sym, symbol);
        if (sym == sym_id) {
            struct symbol *orig_symbol = symbol;

            /* variable declaration */
            if (range_in_sym(storage_class, tmp_obj.storage_class) ||
                range_in_sym(type, tmp_obj.type)) {
                struct variable *var = var_alloc();
                copy_object(&var->object, &tmp_obj);
                list_add_tail(&var->func_scope_node,
                              &sfc->function->func_scope_var_head);
                debug_object(&var->object, "declare var in scope");
            }

            sym = get_token(sfc, &symbol);
            if (sym == sym_eq) {
                /* assignment */
                debug_token(sfc, sym, symbol);
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
            return sym;
        } else if (sym == sym_left_brace) {
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

    list_init(&func->func_scope_var_head);
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

    /*
     * Get the object like:
     * - struct struture
     * - int __attr *function
     */
    if (get_object(sfc, &obj) == -ENODATA)
        return -ENODATA;

    /* Struture */
    // if next token is "id (" is should be function.
    // Otherwise is can be struct declara or var declara.

    /* Function */
    sfc->function = insert_function(sfc->fi, &obj);

    sym = get_token(sfc, &buffer);
    if (sym == sym_left_paren) {
        /* parse te function paramters */
        while (1) {
            struct variable *param = var_alloc();

            sym = get_object(sfc, &param->object);
            /* non-parameter type of function: func(void) */
            if (unlikely(sym != sym_id && param->object.type == sym_void)) {
                free(param);
                break;
            } else
                list_add_tail(&param->parameter_node,
                              &sfc->function->parameter_var_head);
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
        if (sym == sym_seq_point)
            goto out;
        else if (sym == sym_left_brace) {
            /* function definition */
            debug_function(sfc, sfc->function);
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
