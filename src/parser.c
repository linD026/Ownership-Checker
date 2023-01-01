#include <osc/parser.h>
#include <osc/compiler.h>
#include <osc/print.h>
#include <osc/debug.h>
#include <stdio.h>
#include <errno.h>

enum scope_type {
    st_file_scope,
    st_function_scope,
    st_block_scope,
    nr_scope_type,
};

#define MAX_BUFFER_LEN 128

struct scan_file_control {
    struct file_info *fi;
    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned int scope_type;
    struct fsobject_struct *cached_fso;
};

#define for_each_line(sfc)    \
    while ((sfc)->offset = 0, \
           fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL)

#define next_line(sfc)                                                \
    ({                                                                \
        (sfc)->offset = 0;                                            \
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

static __always_inline int decode_obj_action(
    struct scan_file_control *sfc, struct object_struct *obj,
    int (*action)(struct scan_file_control *, struct object_struct *))
{
    if (!action)
        return -EINVAL;
again:
    buffer_for_each (sfc) {
        if (blank(ch))
            continue;
        if (!action(sfc, obj))
            return 0;
    }
    next_line(sfc);
    goto again;
}

static __always_inline int decode_bso_action(
    struct scan_file_control *sfc, struct bsobject_struct *bso,
    int (*action)(struct scan_file_control *, struct bsobject_struct *))
{
    if (!action)
        return -EINVAL;
again:
    buffer_for_each (sfc) {
        if (blank(ch))
            continue;
        if (!action(sfc, bso))
            return 0;
    }
    next_line(sfc);
    goto again;
}

static __always_inline int decode_fso_action(
    struct scan_file_control *sfc, struct fsobject_struct *fso,
    int (*action)(struct scan_file_control *, struct fsobject_struct *))
{
    if (!action)
        return -EINVAL;
again:
    buffer_for_each (sfc) {
        if (blank(ch))
            continue;
        if (!action(sfc, fso))
            return 0;
    }
    next_line(sfc);
    goto again;
}

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

/*
 * File scope object functions
 */

static int decode_type(struct scan_file_control *sfc,
                       struct fsobject_struct *fso)
{
    struct object_type_struct *ot = &fso->info.ot;

    for (unsigned int i = 0; i < nr_object_type; i++) {
        if (type_table[i].len > sfc->size - sfc->offset)
            continue;
        if (strncmp(&sfc->buffer[sfc->offset], type_table[i].type,
                    type_table[i].len) == 0) {
            ot->type = type_table[i].flag;
            /* Now check the pointer type */
            sfc->offset += type_table[i].len;
            decode_obj_action(sfc, &fso->info, check_attr_type);
            decode_obj_action(sfc, &fso->info, check_ptr_type);
            return 0;
        }
    }
    WARN_ON(1, "unkown type:%s", &sfc->buffer[sfc->offset]);
    return -EINVAL;
}

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
        WARN_ON(1, "unkown fsobject:%s", &sfc->buffer[sfc->offset]);
        return -EINVAL;
    }
out:
    sfc->offset++;
    return 0;
}

static int decode_object_name(struct scan_file_control *sfc,
                              struct fsobject_struct *fso)
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
        default:
            fso->info.name[i] = sfc->buffer[sfc->offset++];
        }
    }

    WARN_ON(1, "unkown fsobject name:%s", &sfc->buffer[sfc->offset]);
    return -EINVAL;
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
        decode_fso_action(sfc, sfc->cached_fso, decode_type);
        decode_fso_action(sfc, sfc->cached_fso, decode_object_name);
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
    /* End of the expression line */
    if (sfc->buffer[sfc->offset] == ';')
        return 0;
    if (sfc->buffer[sfc->offset] == '{') {
        /* block scope */
        // create block scope info

        /* Check the next token. */
    }

    return -EAGAIN;
}

static int decode_block_scope_object(struct scan_file_control *sfc,
                                     struct bsobject_struct *bso)
{
    /* End of the function scope */
    if (sfc->buffer[sfc->offset] == '}')
        return 0;

    decode_bso_action(sfc, bso, decode_expr);

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
        decode_fso_action(sfc, fso, decode_type);
        /*
         * Check the object is function and structure declaration/definition,
         * variable declaration.
         * the object of file scope
         */
        /* get the name */
        decode_fso_action(sfc, fso, decode_object_name);
        /* determine the function, structure, or even variable declaration. */
        decode_fso_action(sfc, fso, decode_file_scope_object);

        /* If object is function, decode the function scope  */
        if (fso->fso_type == fso_function) {
            /* prealloc the object.*/
            sfc->cached_fso = fsobject_alloc();
            sfc->cached_fso->fso_type = fso_function_args;
            decode_fso_action(sfc, fso, decode_function);
            WARN_ON(sfc->cached_fso, "unclear the cached object");
            if (fso->fso_type == fso_function_definition) {
                // create block scope info
                struct bsobject_struct *bso = NULL;

                decode_bso_action(sfc, bso, decode_block_scope_object);
            }
        }

        dump_fsobject(fso, "first");

        /* If object is structure, decode the member */
        /* If object is the variable, ... */

        list_for_each_safe (&fso->func_args_head) {
            struct fsobject_struct *tmp =
                container_of(curr, struct fsobject_struct, func_args_node);
            dump_fsobject(tmp, "from head");
        }
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
        /* Initialize the scope to file scope. */
        .scope_type = st_file_scope,
    };

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen");
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
