#ifndef __OSC_OBJECT_PTR_H__
#define __OSC_OBJECT_PTR_H__

#include <osc/compiler.h>

struct scan_file_control;

#define ATTR_FLAGS_BRW 0x0001
#define ATTR_FLAGS_CLONE 0x0002
#define ATTR_FLAGS_MUT 0x0004
#define ATTR_FLAS_MASK (ATTR_FLAGS_BRW | ATTR_FLAGS_CLONE | ATTR_FLAGS_MUT)

/* object */

struct object {
    int storage_class;
    int type;
    // TODO: use the counter
    int is_ptr;
    int attr;
    struct symbol *struct_id;
    struct symbol *id;
};

void object_init(struct object *object);
int cmp_object(struct object *l, struct object *r);
void copy_object(struct object *dst, struct object *src);
int get_attr_flag(int sym);

/* ptr info */

#define PTR_INFO_DROPPED 0x0001
#define PTR_INFO_SET 0x0002
#define PTR_INFO_FUNC_ARG 0x0004

struct ptr_info_internal {
    char buffer[MAX_BUFFER_LEN];
    unsigned long line;
    unsigned int offset;
};

struct ptr_info {
    unsigned int flags;
    struct ptr_info_internal dropped_info;
    struct ptr_info_internal set_info;
};

void record_ptr_info(struct scan_file_control *sfc,
                     struct ptr_info_internal *info);

static inline void ptr_info_mkset(struct ptr_info *info)
{
    info->flags &= ~PTR_INFO_DROPPED;
    info->flags |= PTR_INFO_SET;
}

static inline void ptr_info_mkdropped(struct ptr_info *info)
{
    info->flags |= PTR_INFO_DROPPED;
}

/* var */

struct variable;

void __record_ptr_info(struct ptr_info_internal *info, const char *buffer,
                       unsigned long line, unsigned int offset);
void drop_variable(struct scan_file_control *sfc, struct variable *var);
void set_variable(struct scan_file_control *sfc, struct variable *var);
struct variable *var_alloc(void);
void copy_variable(struct variable *dst, struct variable *src);

#endif /* __OSC_OBJECT_PTR_H__ */
