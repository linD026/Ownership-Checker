#include <osc/parser.h>
#include <osc/debug.h>
#include <osc/check_list.h>
#include <stdio.h>

#define _VA_ENTRY(_type, _TYPE)                                             \
    {                                                                       \
        .type = #_type, .len = sizeof(#_type) - 1, .flag = VAR_ATTR_##_TYPE \
    }
#define VA_ENTRY(type, TYPE) _VA_ENTRY(type, TYPE)

const struct type_info var_attr_table[] = { VA_ENTRY(__brw, BRW),
                                            VA_ENTRY(__mut, MUT) };

void bad(struct scan_file_control *sfc, const char *warning)
{
    print("\e[1m\e[31mOSC ERROR\e[0m\e[0m: \e[1m%s\e[0m\n", warning);
    print("    \e[36m-->\e[0m %s:%lu:%u\n", sfc->fi->name, sfc->line,
          sfc->offset);
    print("    \e[36m|\e[0m    %s", sfc->buffer);
}

void bad_fsobject(struct fsobject_struct *fso)
{
    print("    \e[36m|\e[0m \n"
          "    \e[36m=\e[0m ==== dump object ====\n"
          "    \e[36m||\e[0m function; %s\n"
          "    \e[36m||\e[0m type; %s %s %s\n"
          "    \e[36m||\e[0m name: %s\n"
          "    \e[36m||\e[0m info: %s\n"
          "    \e[36m=\e[0m =====================\n",
          fso->func->info.name, obj_type_name(&fso->info.ot),
          dump_attr(&fso->info), obj_ptr_type(&fso->info.ot) ? "*" : "",
          fso->info.name, dump_fso_type(fso));
}

int check_ownership(void)
{
    return 0;
}

int check_func_args_write(struct scan_file_control *sfc,
                          struct bsobject_struct *bso)
{
    struct fsobject_struct *fso = bso->fso;

    list_for_each (&fso->func_args_head) {
        struct fsobject_struct *fso_arg =
            container_of(curr, struct fsobject_struct, func_args_node);
        if (strncmp(fso_arg->info.name, bso->info.name, MAX_NR_NAME) == 0) {
            struct object_type_struct *arg_ot = &fso_arg->info.ot;
            if (arg_ot->attr_type == VAR_ATTR_DEFAULT) {
                bad(sfc, "Don't write to immutable object");
                bad_fsobject(fso_arg);
            }
            /* For other types, they might be overlapped. */
            if (arg_ot->attr_type & VAR_ATTR_BRW &&
                !(arg_ot->attr_type & VAR_ATTR_MUT)) {
                bad(sfc, "Don't write to borrowed object");
                bad_fsobject(fso_arg);
            }
        }
    }

    return 0;
}
