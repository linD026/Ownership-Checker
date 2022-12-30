#include <osc/parser.h>
#include <osc/debug.h>
#include <stdlib.h>
#include <string.h>

#define _TT_ENTRY(_type, _TYPE)                                                \
    {                                                                          \
        .type = #_type, .len = sizeof(#_type) - 1, .flag = OBJECT_TYPE_##_TYPE \
    }
#define TT_ENTRY(type, TYPE) _TT_ENTRY(type, TYPE)

const struct type_info type_table[] = {
    TT_ENTRY(struct, STRUCT),
    TT_ENTRY(int, INT),
    TT_ENTRY(void, VOID),
};

// TODO: create struct name table to reduce the memory usage.

char *make_obj_struct_name(struct object_type_struct *ot, char *src,
                           unsigned int size)
{
    if (object_type(ot) != OBJECT_TYPE_STRUCT)
        return NULL;

    /* byte align */
    ot->name = malloc(size + 1);
    BUG_ON(!ot->type, "malloc");
    BUG_ON(ot->type & OBJECT_TYPE_STRUCT, "memory lastest bit is not clear");
    strncpy(ot->name, src, size);
    ot->name[size] = '\0';
    ot->type |= OBJECT_TYPE_STRUCT;

    return obj_type_name(ot);
}

void clear_obj_struct_name(struct object_type_struct *ot)
{
    if (object_type(ot) != OBJECT_TYPE_STRUCT)
        return;
    free(obj_type_name(ot));
    ot->type = 0;
}
