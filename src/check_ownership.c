#include <osc/parser.h>
#include <osc/debug.h>
#include <osc/check_list.h>
#include <stdio.h>

// TODO: we should display three information:
// [OPTIONAL] function argument
// [OPTIONAL] ptr assigment
// dropped location
//
static void dump_object(struct object *obj, struct function *func,
                        const char *place)
{
    print("OSC NOTE: The object is declared as %s %s: ", func->object.id->name,
          place);
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
    print("\n");
}

/* checker list */

typedef int (*checker_t)(struct scan_file_control *, struct variable *,
                         struct object *);

static int is_writable(struct scan_file_control *sfc, struct variable *var,
                       struct object *orig_obj)
{
    struct object *obj = &var->object;
    /*
     * For the borrow attribute, we should only check the object
     * instead of ptr. For example, we should check:
     * 
     *      *brw_ptr = ... ;
     * 
     * not the following:
     *
     *      brw_ptr = ... ;
     *
     */
    if ((obj->attr & ATTR_FLAGS_BRW) && !(obj->attr & ATTR_FLAGS_MUT)) {
        if (orig_obj->is_ptr) {
            bad(sfc, "Don't write to the borrowed object");
            return -1;
        }
    }
    if ((obj->attr & ATTR_FLAGS_MUT) &&
        (var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Don't write to the dropped object");
        bad_on_dropped_info(sfc, &var->ptr_info.dropped_info);
        return -1;
    }
    // TODO: To compatible with the normal C,
    // we only check the pointer which has self-defined attributes
    if (0 && !(obj->attr & ATTR_FLAGS_MUT)) {
        bad(sfc, "Don't write to the immutable object");
        return -1;
    }

    return 0;
}

static int is_owned(struct scan_file_control *sfc, struct variable *var,
                    struct object *unused)
{
    struct object *obj = &var->object;

    if ((obj->attr & ATTR_FLAGS_BRW) && !(obj->attr & ATTR_FLAGS_MUT)) {
        bad(sfc,
            "Return the borrowed object which doesn't belong to this function");
        return -1;
    }
    if ((obj->attr & ATTR_FLAGS_MUT) &&
        (var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Return the dropped object");
        bad_on_dropped_info(sfc, &var->ptr_info.dropped_info);
        return -1;
    }

    return 0;
}

static int is_dropped(struct scan_file_control *sfc, struct variable *var,
                      struct object *unused)
{
    struct object *obj = &var->object;

    if ((obj->attr & ATTR_FLAGS_MUT) && (var->ptr_info.flags & PTR_INFO_SET) &&
        !(var->ptr_info.flags & PTR_INFO_DROPPED)) {
        bad(sfc, "Should release the end-of-life object");
        bad_on_set_info(sfc, &var->ptr_info.set_info);
        return -1;
    }

    return 0;
}

/* Common helper functions */

static __always_inline int check_ok(struct scan_file_control *sfc,
                                    struct variable *var, struct object *obj,
                                    checker_t checker)
{
    if (!cmp_token(obj->id, var->object.id))
        return 0;
    if (var->object.type == sym_struct) {
        list_for_each (&var->struct_info.struct_head) {
            struct variable *tmp =
                container_of(curr, struct variable, struct_node);

            if (checker(sfc, tmp, obj)) {
                // TODO: show the struct member
                print("struct member dropped\n");
                return -1;
            }
        }
    }

    return checker(sfc, var, obj);
}

static __always_inline int check_ownership(struct scan_file_control *sfc,
                                           struct object *obj,
                                           checker_t checker)
{
    struct scope_iter_data iter;
    struct function *func = sfc->function;

    list_for_each (&func->parameter_var_head) {
        struct variable *param =
            container_of(curr, struct variable, parameter_node);
        if (check_ok(sfc, param, obj, checker)) {
            dump_object(&param->object, func, "argument");
            return -1;
        }
    }

    for_each_var_in_scopes (func, &iter) {
        struct variable *var = iter.var;
        if (check_ok(sfc, var, obj, checker)) {
            dump_object(&var->object, func, "scope");
            return -1;
        }
    }

    return 0;
}

/* The external functions called in src/parser.c */

#define DEFINE_CHECKER(name, checker)                           \
    int name(struct scan_file_control *sfc, struct object *obj) \
    {                                                           \
        return check_ownership(sfc, obj, checker);              \
    }

DEFINE_CHECKER(check_ownership_writable, is_writable)
DEFINE_CHECKER(check_ownership_owned, is_owned)
DEFINE_CHECKER(check_ownership_dropped, is_dropped)
