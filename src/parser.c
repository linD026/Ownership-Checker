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
    struct object_struct *cached_obj;
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

static __always_inline int
decode_action(struct scan_file_control *sfc, struct object_struct *obj,
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
            ot->attr_type |= var_attr_table[i].flag;
            sfc->offset += var_attr_table[i].len;
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
    WARN_ON(1, "unkown type:%s", &sfc->buffer[sfc->offset]);
    return -EINVAL;
}

static int decode_file_scope_object(struct scan_file_control *sfc,
                                    struct object_struct *obj)
{
    switch (sfc->buffer[sfc->offset]) {
    case '{':
        /* Struture */
        obj->fso_type = fso_structure_definition;
        goto out;
    case '(':
        /* Function */
        obj->fso_type = fso_function;
        goto out;
    default:
        WARN_ON(1, "unkown object:%s", &sfc->buffer[sfc->offset]);
        return -EINVAL;
    }
out:
    sfc->offset++;
    return 0;
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
        default:
            obj->name[i] = sfc->buffer[sfc->offset++];
        }
    }

    WARN_ON(1, "unkown object name:%s", &sfc->buffer[sfc->offset]);
    return -EINVAL;
}

static int decode_function(struct scan_file_control *sfc,
                           struct object_struct *obj)
{
    switch (sfc->buffer[sfc->offset]) {
    case ')':
        sfc->offset++;
        /* we return back to decode handler to help us to check the boundary. */
        return -EAGAIN;
    case '}':
        obj->fso_type = fso_function_definition;
        goto out;
    case ';':
        obj->fso_type = fso_function_declaration;
        goto out;
    default:
        WARN_ON(!sfc->cached_obj, "unallocate object");
        decode_action(sfc, sfc->cached_obj, decode_type);
        decode_action(sfc, sfc->cached_obj, decode_object_name);
        list_add_tail(&sfc->cached_obj->func_args_node, &obj->func_args_head);
        /* Check if there still have argument(s). */
        sfc->cached_obj = object_alloc();
        sfc->cached_obj->fso_type = fso_function_args;
        return -EAGAIN;
    }
    WARN_ON(1, "unexpect reach here");
    return -EINVAL;

out:
    free(sfc->cached_obj);
    sfc->cached_obj = NULL;
    sfc->offset++;
    return 0;
}

static int add_file_list(struct scan_file_control *sfc,
                         struct object_struct *obj)
{
    struct list_head *head;

    switch (obj->fso_type) {
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
    list_add(&obj->node, head);
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
        struct object_struct *obj = object_alloc();

        pr_debug("line: %s", sfc->buffer);
        /*
         * For function and variable, we can get the type first.
         * 
         * the object type of file scope
         */
        decode_action(sfc, obj, decode_type);
        /*
         * Check the object is function and structure declaration/definition,
         * variable declaration.
         * 
         * the object of file scope
         */
        /* get the name */
        decode_action(sfc, obj, decode_object_name);
        /* determine the function, structure, or even variable declaration. */
        decode_action(sfc, obj, decode_file_scope_object);

        dump_object(obj, "first");

        /* If object is function, decode the function scope  */
        if (obj->fso_type == fso_function) {
            /* prealloc the object.*/
            sfc->cached_obj = object_alloc();
            sfc->cached_obj->fso_type = fso_function_args;
            decode_action(sfc, obj, decode_function);
            WARN_ON(sfc->cached_obj, "unclear the cached object");
        }
        /* If object is structure, decode the member */
        /* If object is the variable, ... */

        list_for_each_safe (&obj->func_args_head) {
            struct object_struct *tmp =
                container_of(curr, struct object_struct, func_args_node);
            dump_object(tmp, "from head");
        }
        add_file_list(sfc, obj);
    }

    return 0;
}

static void scan_file(struct scan_file_control *sfc)
{
    for_each_line (sfc) {
        int type = decode(sfc);
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
