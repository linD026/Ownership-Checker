#include <osc/parser.h>
#include <osc/compiler.h>
#include <osc/print.h>
#include <osc/debug.h>
#include <stdio.h>

enum scope_type {
    st_file_scope,
    st_function_scope,
    st_block_scope,
    nr_scope_type,
};

enum decode_type {
    dt_function_declaration,
    dt_function_definition,
    nr_decode_type,
};

#define MAX_BUFFER_LEN 128

struct scan_file_control {
    struct file_info *fi;
    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned int scope_type;
};

#define for_each_line(sfc)    \
    while ((sfc)->offset = 0, \
           fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL)

#define next_line(sfc)                                                \
    ({                                                                \
        (sfc)->offset = 0;                                            \
        (fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL); \
    })

#define buffer_for_each(sfc)                                                  \
    for (char ch = (sfc)->buffer[(sfc)->offset];                              \
         (sfc)->offset < (sfc)->size && (sfc)->buffer[(sfc)->offset] != '\n'; \
         (sfc)->offset += 1, ch = (sfc)->buffer[(sfc)->offset])

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

static int check_ptr_type(struct scan_file_control *sfc,
                          struct object_type_struct *ot)
{
again:
    buffer_for_each (sfc) {
        if (blank(ch))
            continue;
        switch (ch) {
        case '*':
            ot->type |= OBJECT_TYPE_PTR;
        default:
            /* not the pointer symbol, get back one char. */
            sfc->offset--;
            return 0;
        }
    }
    next_line(sfc);
    goto again;
}

static int decode_type(struct scan_file_control *sfc,
                       struct object_type_struct *ot)
{
    pr_debug("line: %s\n", sfc->buffer);
    for (unsigned int i = 0; i < nr_type_table; i++) {
        if (type_table[i].len > sfc->size - sfc->offset)
            continue;
        pr_debug("debug: buf:%s", &sfc->buffer[sfc->offset]);
        if (strncmp(&sfc->buffer[sfc->offset], type_table[i].type,
                    type_table[i].len) == 0) {
            ot->type = type_table[i].flag;
            /* Now check the pointer type */
            sfc->offset += type_table[i].len;
            check_ptr_type(sfc, ot);
            return 1;
        }
    }
    WARN_ON(1, "unkown type:%s", &sfc->buffer[sfc->offset]);
    return 0;
}

static int decode_file_scope_object_type(struct scan_file_control *sfc,
                                         struct object_struct *obj)
{
again:
    buffer_for_each (sfc) {
        if (blank(ch))
            continue;
        if (decode_type(sfc, &obj->ot))
            return 0;
    }
    next_line(sfc);
    goto again;
}

static int decode_file_scope_object(struct scan_file_control *sfc,
                                    struct object_struct *obj)
{
    return 1;
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
        struct object_struct *obj = malloc(sizeof(struct object_type_struct));
        BUG_ON(!obj, "malloc");
        obj->name = NULL;
        obj->ot.type = 0;

        /* For function and variable, we can get the type first. */
        decode_file_scope_object_type(sfc, obj);
        decode_file_scope_object(sfc, obj);

        pr_debug("type: %s %s\n", obj_type_name(&obj->ot),
                 obj_ptr_type(&obj->ot) ? "*" : "");
        //TODO
        free(obj);
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
