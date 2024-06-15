#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>

__allow_unused void __debug_token(struct scan_file_control *sfc, int sym,
                                  struct symbol *symbol)
{
#ifdef CONFIG_DEBUG
    if (symbol == NULL) {
        int __c = debug_sym_one_char(sym);
        if (__c != -ENODATA)
            print("char          : |%c|\n", (char)__c);
    } else
        print("symbol(%2d, %2u): |%s|\n", symbol->flags, symbol->len,
              symbol->name);
#endif /* CONFIG_DEBUG */
}

static __allow_unused void raw_debug_object(struct object *obj)
{
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
}

void debug_object(struct object *obj, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[OBJECT] %s: ", note);
    raw_debug_object(obj);
    print(" \n");
#endif /* CONFIG_DEBUG */
}

void debug_variable(struct variable *var, const char *note)
{
#ifdef CONFIG_DEBUG
    struct ptr_info *info = &var->ptr_info;
    print("[VAR] ");
    debug_object(&var->object, note);
    if (info->flags & PTR_INFO_FUNC_ARG) {
        print("[VAR] is func parrameter\n");
    }
    if (info->flags & PTR_INFO_SET) {
        print("[VAR] set at:%ld:%u\n", info->set_info.line,
              info->set_info.offset);
    }
    if (info->flags & PTR_INFO_DROPPED) {
        print("[VAR] dropped at:%ld:%u\n", info->dropped_info.line,
              info->dropped_info.offset);
    }
#endif
}

static __allow_unused void debug_space_level(int nested_level)
{
    for (int i = 0; i < nested_level; i++)
        print("    ");
}

static __allow_unused void raw_debug_structure(struct structure *structure,
                                               int nested_level)
{
    print("struct %s ", structure->object.struct_id->name);
    print("{\n");
    list_for_each (&structure->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);
        debug_space_level(nested_level);
        if (mem->object.type == sym_struct)
            raw_debug_structure(&mem->struct_info, nested_level + 1);
        else {
            raw_debug_object(&mem->object);
            print(";\n");
        }
    }

    if (nested_level > 1)
        debug_space_level(nested_level - 1);

    if (structure->object.id) {
        print("} %s;\n", structure->object.id->name);
    } else {
        print("};\n");
    }
}

void debug_structure(struct structure *structure, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[STRUCT START]: %s\n", note);
    raw_debug_structure(structure, 1);
    print("[STRUCT END]\n");
#endif /* CONFIG_DEBUG */
}

void debug_function(struct function *function)
{
#ifdef CONFIG_DEBUG
    print("[FUNC] ");
    raw_debug_object(&function->object);
    print(" (");
    if (unlikely(list_empty(&function->parameter_head)))
        print("void");
    else {
        list_for_each (&function->parameter_head) {
            struct variable *param =
                container_of(curr, struct variable, parameter_node);
            raw_debug_object(&param->object);
            if (curr->next != &function->parameter_head)
                print(", ");
        }
    }
    print(")");

    // TODO: scope object
    print("\n");
#endif /* CONFIG_DEBUG */
}
