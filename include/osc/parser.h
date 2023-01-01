#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <osc/debug.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct type_info {
    char *type;
    unsigned int len;
    unsigned int flag;
};

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
    // object type (int, void, etc)
    union {
        char *name;
        uintptr_t type;
    };
    // attribute type (__brw, __mut, etc)
    unsigned int attr_type;
};

/* variable attribute */

#define VAR_ATTR_DEFAULT 0x0000U
#define VAR_ATTR_BRW 0x0001U
#define VAR_ATTR_MUT 0x0002U
#define VAR_ATTR_BRW_ONCE (VAR_ATTR_BRW | VAR_ATTR_MUT)

#define nr_var_attr 2
/* Define in src/check_ownership.c */
extern const struct type_info var_attr_table[];

/* object_type_struct->type */
#define OBJECT_TYPE_NONE 0x0000U
#define OBJECT_TYPE_PTR 0x0001U
#define OBJECT_TYPE_STRUCT 0x0002U
#define OBJECT_TYPE_INT 0x0004U
#define OBJECT_TYPE_VOID 0x0008U

#define OBJET_TYPE_EXCLUDE_PTR_MASK \
    (OBJECT_TYPE_STRUCT | OBJECT_TYPE_INT | OBJECT_TYPE_VOID)

enum {
    tt_struct,
    tt_int,
    tt_void,
    nr_object_type,
};

/* Define in src/object_type.c */
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
        case OBJECT_TYPE_NONE:
            return "";
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

/* Object and file info */

#define MAX_NR_NAME 80

struct fsobject_struct;

struct object_struct {
    char name[MAX_NR_NAME];
    /* If it is the function this is return type. */
    struct object_type_struct ot;
};

void object_init(struct object_struct *obj);

/* block scope object */
struct bsobject_struct {
    struct object_struct info;
    union {
        struct list_head block_scope_node;
        struct list_head block_scope_head;
    };
    struct fsobject_struct *fso;
};

struct bsobject_struct *bsobject_alloc(struct fsobject_struct *fso);

/* file scope object */
struct fsobject_struct {
    struct object_struct info;
    /* function/structure/variable declaration type */
    unsigned int fso_type;
    /* Could be function or variable type */
    struct list_head node;
    union {
        struct list_head func_args_node;
        struct list_head func_args_head;
    };
};

struct fsobject_struct *fsobject_alloc(void);

/* fsobject_struct->fso_type */
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

/* dump info */

static inline char *dump_attr(struct object_struct *obj)
{
    struct object_type_struct *ot = &obj->ot;

    switch (ot->attr_type) {
    case VAR_ATTR_DEFAULT:
        return "";
    case VAR_ATTR_BRW:
        return "__brw";
    case VAR_ATTR_MUT:
        return "__mut";
    case VAR_ATTR_BRW_ONCE:
        return "__mut __brw";
    default:
        WARN_ON(1, "unkown attr type:%u", ot->attr_type);
    }
    return NULL;
}

static inline char *dump_fso_type(struct fsobject_struct *fso)
{
    switch (fso->fso_type) {
#define SWITCH_FSO_ENTRY(type) \
    case type:                 \
        return #type;
        SWITCH_FSO_ENTRY(fso_unkown)
        SWITCH_FSO_ENTRY(fso_function)
        SWITCH_FSO_ENTRY(fso_function_declaration)
        SWITCH_FSO_ENTRY(fso_function_definition)
        SWITCH_FSO_ENTRY(fso_function_args)
        SWITCH_FSO_ENTRY(fso_structure_definition)
        SWITCH_FSO_ENTRY(fso_variable_declaration)
#undef SWITCH_FSO_ENTRY
    default:
        WARN_ON(1, "unkown attr type:%u", fso->fso_type);
    }
    return NULL;
}

#define dump_fsobject(fso, fmt, ...)                                  \
    do {                                                              \
        print("\n"                                                    \
              "==== dump object ====\n"                               \
              "type; %s %s %s\n"                                      \
              "name: %s\n"                                            \
              "fso_type: %s\n"                                        \
              "---------------------\n"                               \
              "note: " fmt "\n"                                       \
              "=====================\n",                              \
              obj_type_name(&fso->info.ot), dump_attr(&fso->info),    \
              obj_ptr_type(&fso->info.ot) ? "*" : "", fso->info.name, \
              dump_fso_type(fso), ##__VA_ARGS__);                     \
    } while (0)

#define dump_bsobject(bso, fmt, ...)                                  \
    do {                                                              \
        print("\n"                                                    \
              "==== dump object ====\n"                               \
              "type; %s %s %s\n"                                      \
              "name: %s\n"                                            \
              "---------------------\n"                               \
              "note: " fmt "\n"                                       \
              "=====================\n",                              \
              obj_type_name(&bso->info.ot), dump_attr(&bso->info),    \
              obj_ptr_type(&bso->info.ot) ? "*" : "", bso->info.name, \
              ##__VA_ARGS__);                                         \
    } while (0)

/* Parser structures and functions */

struct file_info {
    char name[MAX_NR_NAME];
    /* For osc data struct in src/osc.c */
    struct list_head node;
    struct list_head func_head;
    FILE *file;
};

enum scope_type {
    st_file_scope,
    st_function_scope,
    st_block_scope,
    nr_scope_type,
};

#define MAX_BUFFER_LEN 128

struct scan_file_control {
    struct file_info *fi;
    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned long line;
    unsigned int scope_type;
    struct fsobject_struct *cached_fso;
};

int parser(struct file_info *fi);

#endif /* __OSC_PARSER_H__ */
