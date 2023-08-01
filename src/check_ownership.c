#include <osc/parser.h>
#include <osc/debug.h>
#include <osc/check_list.h>
#include <stdio.h>

static void dump_object(struct object *obj, struct function *func,
                        const char *place)
{
    print("OSC NOTE: The object is declared as %s's %s: ",
          func->object.id->name, place);
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
    print("\n");
}

static int is_same_and_writeable(struct scan_file_control *sfc,
                                 struct variable *var, struct object *obj)
{
    struct object *orig = &var->object;

    if (!cmp_token(obj->id, orig->id))
        return 0;
    if ((orig->attr & ATTR_FLAGS_BRW) && !(orig->attr & ATTR_FLAGS_MUT)) {
        bad(sfc, "Don't write to the borrowed object");
        return -1;
    }
    if ((orig->attr & ATTR_FLAGS_MUT) && var->is_dropped) {
        struct dropped_info *info = &var->dropped_info;

        bad(sfc, "Don't write to the dropped object");
        print("    \e[36m+->\e[0m Dropped at %s:%lu:%u\n", sfc->fi->name,
              info->line, info->offset);
        print("    \e[36m|\e[0m    %s", info->buffer);
        print("    \e[36m|\e[0m   ");
        for (int i = 0; i < info->offset; i++)
            print(" ");
        print("\e[31m^\e[0m\n");
        return -1;
    }
    // TODO: To compatible with the normal C,
    // we only check the pointer which has self-defined attributes
    if (0 && !(orig->attr & ATTR_FLAGS_MUT)) {
        bad(sfc, "Don't write to the immutable object");
        return -1;
    }

    return 0;
}

int check_ownership_writeable(struct scan_file_control *sfc, struct object *obj)
{
    struct function *func = sfc->function;

    list_for_each (&func->parameter_var_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        if (is_same_and_writeable(sfc, param, obj)) {
            dump_object(&param->object, func, "argument");
            return -1;
        }
    }

    list_for_each (&func->func_scope_var_head) {
        struct variable *var =
            container_of(curr, struct variable, func_scope_node);
        if (is_same_and_writeable(sfc, var, obj)) {
            dump_object(&var->object, func, "scope");
            return -1;
        }
    }

    return 0;
}
