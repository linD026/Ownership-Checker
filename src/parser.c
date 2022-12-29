#include <osc/parser.h>
#include <osc/object_type.h>
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

struct scan_file_control {
    struct file_info *fi;
    unsigned int scope_type;
};

#define MAX_BUFFER_LEN 128

#define for_each_line(file, buf, size) while (fgets(buf, size, file) != NULL)

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

static int decode_type(struct object_type_struct *ot, char *buffer,
                       unsigned int buffer_offset, unsigned int size)
{
    pr_debug("line: %s\n", buffer);
    for (unsigned int i = 0; i < nr_type_table; i++) {
        if (type_table[i].len > size - buffer_offset)
            continue;
        if (strncmp(&buffer[buffer_offset], type_table[i].type,
                    type_table[i].len) == 0) {
            ot->type = type_table[i].flag;
            return 1;
        }
    }
    WARN_ON(1, "unkown type:%s", &buffer[buffer_offset]);
    return 0;
}

static struct object_type_struct *decode_file_scope_type(char *buffer,
                                                         unsigned int size)
{
    char ch;
    struct object_type_struct *ot = malloc(sizeof(struct object_type_struct));
    BUG_ON(!ot, "malloc");

    for (unsigned int i = 0; i < size && buffer[i] != '\n'; i++) {
        ch = buffer[i];
        if (blank(ch))
            continue;
        if (decode_type(ot, buffer, i, size))
            return ot;
    }
    /* Unkown type, release the ot. */
    free(ot);
    return NULL;
}

static int decode(struct scan_file_control *sfc, char *buffer,
                  unsigned int size)
{
    /*
     * If current scope type is file, two cases here:
     * function: declaration and definition
     * variable: declaration
     * marco: TODO
     */
    if (sfc->scope_type == st_file_scope) {
        /* For function and variable, we can get the type first. */
        struct object_type_struct *ot = decode_file_scope_type(buffer, size);

        pr_debug("type: %s\n", obj_type_name(ot));
        //TODO
        free(ot);
    }

    return 0;
}

static void scan_file(struct scan_file_control *sfc)
{
    char buffer[MAX_BUFFER_LEN];

    for_each_line(sfc->fi->file, buffer, MAX_BUFFER_LEN)
    {
        int type = decode(sfc, buffer, MAX_BUFFER_LEN);
    }
}

int parser(struct file_info *fi)
{
    struct scan_file_control sfc = {
        .fi = fi,
        /* Initialize the scope to file scope. */
        .scope_type = st_file_scope,
    };

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen");
    scan_file(&sfc);
    fclose(fi->file);
}
