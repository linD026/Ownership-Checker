#include <osc/parser.h>
#include <osc/list.h>
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

void object_init(struct object_struct *obj)
{
    memset(obj->name, '\0', MAX_NR_NAME);
    obj->ot.type = OBJECT_TYPE_NONE;
    obj->ot.attr_type = VAR_ATTR_DEFAULT;
}

struct bsobject_struct *bsobject_alloc(struct fsobject_struct *fso)
{
    struct bsobject_struct *bso = malloc(sizeof(struct bsobject_struct));
    BUG_ON(!bso, "malloc");

    object_init(&bso->info);
    list_init(&bso->block_scope_node);
    bso->fso = fso;

    return bso;
}

struct fsobject_struct *fsobject_alloc(void)
{
    struct fsobject_struct *fso = malloc(sizeof(struct fsobject_struct));
    BUG_ON(!fso, "malloc");

    object_init(&fso->info);
    fso->fso_type = fso_unkown;
    list_init(&fso->node);
    list_init(&fso->func_args_node);

    return fso;
}
