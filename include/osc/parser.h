#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <osc/debug.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 *  Object type info
 *
 *  struct object_type_struct {
 *      union {
 *          if (type == struct)
 *              char *name;
 *          else
 *              unsigned int type;
 *      };
 *  };
 */
struct object_type_struct {
    union {
        char *name;
        uintptr_t type;
    };
};

#define OBJECT_TYPE_STRUCT 0x0001U
#define OBJECT_TYPE_PTR 0x0002U
#define OBJECT_TYPE_INT 0x0004U
#define OBJECT_TYPE_VOID 0x0008U

#define OBJET_TYPE_EXCLUDE_PTR_MASK \
    (OBJECT_TYPE_STRUCT | OBJECT_TYPE_INT | OBJECT_TYPE_VOID)

struct type_info {
    char *type;
    unsigned int len;
    unsigned int flag;
};

enum {
    tt_struct,
    tt_int,
    tt_void,
    nr_type_table,
};

extern const struct type_info type_table[];

static inline char *obj_type_name(struct object_type_struct *ot)
{
    if (!(ot->type & OBJECT_TYPE_STRUCT)) {
        switch (ot->type & OBJET_TYPE_EXCLUDE_PTR_MASK) {
#define SWITCH_TYPE_ENTRY(_type, _TYPE) \
    case OBJECT_TYPE_##_TYPE:           \
        return type_table[tt_##_type].type;
            SWITCH_TYPE_ENTRY(int, INT)
            SWITCH_TYPE_ENTRY(void, VOID)
#undef SWITCH_TYPE_ENTRY
        default:
            WARN_ON(1, "unkown type:%lu", ot->type);
            return NULL;
        }
    }
    return (char *)(ot->type & ~OBJECT_TYPE_STRUCT);
}

static inline int obj_ptr_type(struct object_type_struct *ot)
{
    return (ot->type & OBJECT_TYPE_PTR) ? 1 : 0;
}

char *make_obj_struct_name(struct object_type_struct *ot, char *src,
                           unsigned int size);
void clear_obj_struct_name(struct object_type_struct *ot);

static inline unsigned int object_type(struct object_type_struct *ot)
{
    if (ot->type & OBJECT_TYPE_STRUCT)
        return OBJECT_TYPE_STRUCT;
    return ot->type;
}

static inline int obj_type_same(struct object_type_struct *a,
                                struct object_type_struct *b)
{
    if (a->type & OBJECT_TYPE_STRUCT && b->type & OBJECT_TYPE_STRUCT)
        return (strcmp(obj_type_name(a), obj_type_name(b)) == 0);
    return a->type == b->type;
}

struct object_struct *object_alloc(void);

/* Object and file info */

#define MAX_NR_NAME 80

struct object_struct {
    char name[MAX_NR_NAME];
    /* file scope object */
    /* If it is the function this is return type. */
    struct object_type_struct ot;
    /* function/structure/variable declaration type */
    unsigned int fso_type;
    union {
        struct list_head func_args_node;
        struct list_head func_args_head;

        struct list_head scope_head;
    };
    /* Could be function or variable type */
    struct list_head node;
};

enum file_scope_object_type {
    fso_unkown,
    fso_function,
    /* EX: void func(void); */
    fso_function_declaration,
    /* EX: void func(void) { ... } */
    fso_function_definition,
    /* EX: void func (int a) { ... } */
    fso_function_args,
    /* EX: struct name { ... } */
    fso_structure_definition,
    /* EX: struct name var = ...; */
    fso_variable_declaration,
    nr_file_scope_object_type,
};

struct file_info {
    char name[MAX_NR_NAME];
    /* For osc data struct in src/osc.c */
    struct list_head node;
    struct list_head func_head;
    FILE *file;
};

int parser(struct file_info *fi);

#endif /* __OSC_PARSER_H__ */
