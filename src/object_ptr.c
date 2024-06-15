#include <osc/parser.h>
#include <osc/token.h>
#include <osc/object_ptr.h>

void object_init(struct object *object)
{
    object->storage_class = sym_dump;
    object->type = sym_dump;
    object->is_ptr = 0;
    object->attr = 0;
    object->struct_id = NULL;
    object->id = NULL;
}

int cmp_object(struct object *l, struct object *r)
{
    if (l->storage_class != r->storage_class)
        return 0;
    if (l->type != r->type)
        return 0;
    if (l->is_ptr != r->is_ptr)
        return 0;
    if (l->attr != r->attr)
        return 0;
    // TODO: We should improve this condition checkings...
    if (l->struct_id && r->struct_id) {
        if (!cmp_token(l->struct_id, r->struct_id))
            return 0;
    } else if ((l->struct_id && !r->struct_id) ||
               (!l->struct_id && r->struct_id))
        return 0;
    if (!cmp_token(l->id, r->id))
        return 0;
    return 1;
}

void copy_object(struct object *dst, struct object *src)
{
    *dst = *src;
}

int get_attr_flag(int sym)
{
    switch (sym) {
    case sym_attr_brw:
        return ATTR_FLAGS_BRW;
    case sym_attr_clone:
        return ATTR_FLAGS_CLONE;
    case sym_attr_mut:
        return ATTR_FLAGS_MUT;
    }
    return 0;
}

void __record_ptr_info(struct ptr_info_internal *info, const char *buffer,
                       unsigned long line, unsigned int offset)
{
    strncpy(info->buffer, buffer, MAX_BUFFER_LEN);
    info->line = line;
    /*
     * We adapt the offset to the last symbol when we report the warning.
     * See the bad_get_last_offset();
     */
    info->offset = offset;
}

void record_ptr_info(struct scan_file_control *sfc,
                     struct ptr_info_internal *info)
{
    __record_ptr_info(info, sfc->buffer, sfc->line, sfc->offset);
}

void drop_variable(struct scan_file_control *sfc, struct variable *var)
{
    // TODO: don't just warn it
    WARN_ON(!(var->ptr_info.flags & (PTR_INFO_SET | PTR_INFO_FUNC_ARG)),
            "drop the unassigned ptr");
    record_ptr_info(sfc, &var->ptr_info.dropped_info);
    ptr_info_mkdropped(&var->ptr_info);
    debug_ptr_info(&var->ptr_info.dropped_info, NULL);
}

void set_variable(struct scan_file_control *sfc, struct variable *var)
{
#ifdef CONFIG_DEBUG
    if (var->ptr_info.flags & PTR_INFO_DROPPED) {
        pr_debug("variable %s; re-assigned after dropped\n",
                 var->object.id->name);
    }
#endif
    record_ptr_info(sfc, &var->ptr_info.set_info);
    ptr_info_mkset(&var->ptr_info);
    debug_ptr_info(&var->ptr_info.set_info, NULL);
}

struct variable *var_alloc(void)
{
    struct variable *var = malloc(sizeof(struct variable));
    BUG_ON(!var, "malloc");

    var->ptr_info.flags = 0;
    object_init(&var->object);
    list_init(&var->struct_info.struct_head);
    list_init(&var->struct_info.node);
    list_init(&var->scope_node);
    list_init(&var->struct_node);
    list_init(&var->parameter_node);

    return var;
}

void copy_variable(struct variable *dst, struct variable *src)
{
    dst->ptr_info.flags = src->ptr_info.flags;
    if (dst->ptr_info.flags & PTR_INFO_SET) {
        __record_ptr_info(
            &dst->ptr_info.set_info, src->ptr_info.set_info.buffer,
            src->ptr_info.set_info.line, src->ptr_info.set_info.offset);
    }
    if (dst->ptr_info.flags & PTR_INFO_DROPPED) {
        __record_ptr_info(
            &dst->ptr_info.dropped_info, src->ptr_info.dropped_info.buffer,
            src->ptr_info.dropped_info.line, src->ptr_info.dropped_info.offset);
    }

    copy_object(&dst->object, &src->object);
}

/* Structure */

void copy_structure(struct structure *dst, struct structure *src)
{
    copy_object(&dst->object, &src->object);

    list_for_each (&src->struct_head) {
        struct variable *src_mem =
            container_of(curr, struct variable, struct_node);
        struct variable *dst_var = var_alloc();

        if (src_mem->object.type == sym_struct)
            copy_structure(&dst_var->struct_info, &src_mem->struct_info);
        else
            copy_object(&dst_var->object, &src_mem->object);
        list_add_tail(&dst_var->struct_node, &dst->struct_head);
    }
}

/* @obj should be the id */
struct structure *search_structure(struct scan_file_control *sfc,
                                   struct object *obj)
{
    list_for_each (&sfc->fi->struct_head) {
        struct structure *tmp = container_of(curr, struct structure, node);
        if (cmp_token(obj->struct_id, tmp->object.struct_id))
            return tmp;
    }

    bad(sfc, "undefined structure type");
    return NULL;
}

void set_struct_member(struct scan_file_control *sfc, struct structure *s,
                       struct object *obj)
{
    list_for_each (&s->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);

        if (cmp_token(mem->object.id, obj->id)) {
            set_variable(sfc, mem);
            return;
        }
    }

    bad(sfc, "undefined structure member");
}

void drop_struct_member(struct scan_file_control *sfc, struct structure *s,
                        struct object *obj)
{
    list_for_each (&s->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);

        if (cmp_token(mem->object.id, obj->id)) {
            drop_variable(sfc, mem);
            return;
        }
    }

    bad(sfc, "undefined structure member");
    debug_structure(s, "drop struct member");
}
