#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/print.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define for_each_line(sfc)                   \
    while ((sfc)->offset = 0, (sfc)->line++, \
           fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL)

#define next_line(sfc)                                                    \
    ({                                                                    \
        (sfc)->offset = 0;                                                \
        (sfc)->line++,                                                    \
            (fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL); \
    })

#define line_end(sfc) \
    ((sfc)->offset >= (sfc)->size || (sfc)->buffer[(sfc)->offset] == '\n')

#define buffer_for_each(sfc)                                     \
    for (char ch = (sfc)->buffer[(sfc)->offset]; !line_end(sfc); \
         ch = (sfc)->buffer[++(sfc)->offset])

static __always_inline int blank(char ch)
{
    switch (ch) {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    case '\r':
        return 1;
    }
    return 0;
}

/*
 * It will get the next non-blank symbols. it's same as following function:
 *
 *   static __always_inline int decode_obj_action(
 *       struct scan_file_control *sfc, struct object_struct *obj,
 *       int (*action)(struct scan_file_control *, struct object_struct *))
 *   {
 *       if (!action)
 *           return -EINVAL;
 *   again:
 *       buffer_for_each (sfc) {
 *           if (blank(ch))
 *               continue;
 *           if (!action(sfc, obj))
 *               return 0;
 *       }
 *       next_line(sfc);
 *       goto again;
 *   }
 *
 */
#define ___decode_action(sfc, obj, action, id, ret) \
    do {                                            \
        da_##id##_again : buffer_for_each (sfc)     \
        {                                           \
            if (blank(ch))                          \
                continue;                           \
            ret = action(sfc, obj);                 \
            if (ret != -EAGAIN)                     \
                goto da_##id##_out;                 \
        }                                           \
        next_line(sfc);                             \
        goto da_##id##_again;                       \
        da_##id##_out:;                             \
    } while (0)

#define __decode_action(sfc, obj, action, id)                         \
    ({                                                                \
        int __da_ret = 0;                                             \
        ___decode_action(sfc, obj, action, id, __da_ret);             \
        WARN_ON(__da_ret < 0, "action:%s, error:%d, buf:%s", #action, \
                __da_ret, &sfc->buffer[sfc->offset]);                 \
        __da_ret;                                                     \
    })

#define decode_action(sfc, obj, action) \
    __decode_action(sfc, obj, action, __LINE__)

#define __try_decode_action(sfc, obj, action, id)         \
    ({                                                    \
        int __da_ret = 0;                                 \
        ___decode_action(sfc, obj, action, id, __da_ret); \
        __da_ret;                                         \
    })

#define try_decode_action(sfc, obj, action) \
    __try_decode_action(sfc, obj, action, __LINE__)

static __always_inline int check_ptr_type(struct scan_file_control *sfc,
                                          struct object_struct *obj)
{
    struct object_type_struct *ot = &obj->ot;

    switch (sfc->buffer[sfc->offset]) {
    case '*':
        ot->type |= OBJECT_TYPE_PTR;
        /* pointer symbol, skip one char. */
        sfc->offset++;
        return 0;
    default:
        if (unlikely(ot->attr_type & VAR_ATTR_BRW))
            bad(sfc, "__brw with non pointer object");
        return 0;
    }
    return 0;
}

static __always_inline int check_attr_type(struct scan_file_control *sfc,
                                           struct object_struct *obj)
{
    struct object_type_struct *ot = &obj->ot;

    for (unsigned int i = 0; i < nr_var_attr; i++) {
        if (var_attr_table[i].len > sfc->size - sfc->offset)
            continue;
        if (strncmp(&sfc->buffer[sfc->offset], var_attr_table[i].type,
                    var_attr_table[i].len) == 0) {
            WARN_ON(ot->attr_type & var_attr_table[i].flag,
                    "duplicate attr type:%s", var_attr_table[i].type);
            ot->attr_type |= var_attr_table[i].flag;
            sfc->offset += var_attr_table[i].len;
            return -EAGAIN;
        }
    }
    return 0;
}

static int decode_type(struct scan_file_control *sfc, struct object_struct *obj)
{
    struct object_type_struct *ot = &obj->ot;

    for (unsigned int i = 0; i < nr_object_type; i++) {
        if (type_table[i].len > sfc->size - sfc->offset)
            continue;
        if (strncmp(&sfc->buffer[sfc->offset], type_table[i].type,
                    type_table[i].len) == 0) {
            ot->type = type_table[i].flag;
            /* Now check the pointer type */
            sfc->offset += type_table[i].len;
            decode_action(sfc, obj, check_attr_type);
            decode_action(sfc, obj, check_ptr_type);
            return 0;
        }
    }

    return -EINVAL;
}

static int decode_object_name(struct scan_file_control *sfc,
                              struct object_struct *obj)
{
    for (unsigned int i = 0; i < MAX_NR_NAME; i++) {
        if (line_end(sfc))
            return 0;
        switch (sfc->buffer[sfc->offset]) {
        /* EX: func(int a, int b); */
        case ',':
        case ')':
        /* EX: struct name {  ... } or void func(...) */
        case '{':
        case '(':
            return 0;
        /* EX: ... name = ... */
        case '=':
            sfc->offset++;
            return 1;
        /* EX: name = .... or name ( ..) */
        case ' ':
            /* check next symbol */
            sfc->offset++;
            break;
        default:
            obj->name[i] = sfc->buffer[sfc->offset++];
        }
    }

    return -EINVAL;
}

/*
 * File scope object functions
 */

static int decode_file_scope_object(struct scan_file_control *sfc,
                                    struct fsobject_struct *fso)
{
    switch (sfc->buffer[sfc->offset]) {
    case '{':
        /* Struture */
        fso->fso_type = fso_structure_definition;
        goto out;
    case '(':
        /* Function */
        fso->fso_type = fso_function;
        goto out;
    default:
        return -EINVAL;
    }
out:
    sfc->offset++;
    return 0;
}

static int decode_function(struct scan_file_control *sfc,
                           struct fsobject_struct *fso)
{
    switch (sfc->buffer[sfc->offset]) {
    case ')':
        sfc->offset++;
        /* we return back to decode handler to help us to check the boundary. */
        return -EAGAIN;
    case '{':
        fso->fso_type = fso_function_definition;
        goto out;
    case ';':
        fso->fso_type = fso_function_declaration;
        goto out;
    default:
        WARN_ON(!sfc->cached_fso, "unallocate fsobject");
        sfc->cached_fso->func = fso;
        decode_action(sfc, &sfc->cached_fso->info, decode_type);
        decode_action(sfc, &sfc->cached_fso->info, decode_object_name);
        list_add_tail(&sfc->cached_fso->func_args_node, &fso->func_args_head);
        /* Check if there still have argument(s). */
        sfc->cached_fso = fsobject_alloc();
        sfc->cached_fso->fso_type = fso_function_args;
        return -EAGAIN;
    }
    WARN_ON(1, "unexpect reach here");
    return -EINVAL;

out:
    free(sfc->cached_fso);
    sfc->cached_fso = NULL;
    sfc->offset++;
    return 0;
}

/*
 * Block scope object functions
 */
static int decode_expr(struct scan_file_control *sfc,
                       struct bsobject_struct *bso)
{
    pr_debug("expr:%s\n", &sfc->buffer[sfc->offset]);
    if (sfc->buffer[sfc->offset] == ';')
        return 0;
    return -EAGAIN;
}

static int decode_block_scope_object(struct scan_file_control *sfc,
                                     struct bsobject_struct *bso);

static int decode_stmt(struct scan_file_control *sfc,
                       struct bsobject_struct *bso)
{
    struct bsobject_struct *lvalue = NULL;

    pr_debug("buffer:%s\n", &sfc->buffer[sfc->offset]);
    /* End of the block scope */
    if (sfc->buffer[sfc->offset] == '}')
        return 0;
    /* New block scope */
    if (sfc->buffer[sfc->offset] == '{') {
        /* block scope - create block scope info */
        struct bsobject_struct *new = bsobject_alloc(bso->fso);
        
        list_add_tail(&new->func_block_scope_node,
                      &new->fso->func_block_scope_head);
        sfc->offset++;
        decode_action(sfc, new, decode_stmt);
        return 0;
    }

    /*
     * get lvalue
     * Check the write operation: lvalue = expr
     * We should check the attr type of lvalue.
     */
    lvalue = bsobject_alloc(bso->fso);
    /* 
     * First try to get the type.
     * decoded the type, it is the variable declaration.
     */
    if (!try_decode_action(sfc, &lvalue->info, decode_type)) {
        /* lvalue is declaration */
        list_add_tail(&lvalue->var_declaration_node,
                      &bso->var_declaration_head);
    } else {
        /* Check pointer type. */
        try_decode_action(sfc, &lvalue->info, check_ptr_type);
    }
    decode_action(sfc, &lvalue->info, decode_object_name);
    check_func_args_write(sfc, lvalue);
    // TODO: what if the function call?
    decode_action(sfc, lvalue, decode_expr);

    // check attr type
    // variable declaration?

    return -EAGAIN;
}

static int decode_block_scope_object(struct scan_file_control *sfc,
                                     struct bsobject_struct *bso)
{
    /* End of the function scope */
    if (sfc->buffer[sfc->offset] == '}')
        return 0;

    decode_action(sfc, bso, decode_stmt);

    return -EAGAIN;
}

/*
 * Parser related functions
 */

static int add_file_list(struct scan_file_control *sfc,
                         struct fsobject_struct *fso)
{
    struct list_head *head;

    switch (fso->fso_type) {
    case fso_function:
    case fso_function_declaration:
    case fso_function_definition:
        head = &sfc->fi->func_head;
        break;
    default:
        //TODO: struct, var
        WARN_ON(1, "unsupport type");
        return -EINVAL;
    }
    list_add(&fso->node, head);
    return 0;
}

static int decode(struct scan_file_control *sfc)
{
    /*
     * If current scope type is file, two cases here:
     * function: declaration and definition
     * variable: declaration
     * marco: TODO
     */
    if (sfc->scope_type == st_file_scope) {
        struct fsobject_struct *fso = fsobject_alloc();

        pr_debug("line: %s", sfc->buffer);
        /*
         * For function and variable, we can get the type first.
         * the object type of file scope
         */
        decode_action(sfc, &fso->info, decode_type);
        /*
         * Check the object is function and structure declaration/definition,
         * variable declaration.
         * the object of file scope
         */
        /* get the name */
        decode_action(sfc, &fso->info, decode_object_name);
        /* determine the function, structure, or even variable declaration. */
        decode_action(sfc, fso, decode_file_scope_object);

        /* If object is function, decode the function scope  */
        if (fso->fso_type == fso_function) {
            /* prealloc the object.*/
            sfc->cached_fso = fsobject_alloc();
            sfc->cached_fso->fso_type = fso_function_args;
            decode_action(sfc, fso, decode_function);
            WARN_ON(sfc->cached_fso, "unclear the cached object");
            if (fso->fso_type == fso_function_definition) {
                // create block scope info
                struct bsobject_struct *bso = bsobject_alloc(fso);

                list_add_tail(&bso->func_block_scope_node,
                              &fso->func_block_scope_head);
                dump_fsobject(fso, "first");
                list_for_each_safe (&fso->func_args_head) {
                    struct fsobject_struct *tmp = container_of(
                        curr, struct fsobject_struct, func_args_node);
                    dump_fsobject(tmp, "from head");
                }
                decode_action(sfc, bso, decode_block_scope_object);
            }
        }

        /* If object is structure, decode the member */
        /* If object is the variable, ... */

        add_file_list(sfc, fso);
    }

    return 0;
}

static void scan_file(struct scan_file_control *sfc)
{
    for_each_line (sfc) {
        decode(sfc);
    }
}

int parser(struct file_info *fi)
{
    struct scan_file_control sfc = {
        .fi = fi,
        .size = MAX_BUFFER_LEN,
        .offset = 0,
        .line = 0,
        /* Initialize the scope to file scope. */
        .scope_type = st_file_scope,
    };

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen");
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
