#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/object_ptr.h>
#include <osc/list.h>
#include <osc/token.h>
#include <osc/debug.h>

struct symbol {
    char *name;
    unsigned int len;
    int flags;
};

/*
 * To reduce the maintainability, this is for the type information
 * and the variable information for structure. So that we can easily
 * create the new variable by duplicating the type info.
 */
struct structure {
    /*
     * struct info
     * NOTE: Make sure @object is the first member to
     * align with the @object in struct variable.
     * See the comments in struct variable.
     */
    struct object object;

    /* variable */
    struct list_head struct_head;

    /* For struct file_info */
    struct list_head node;
};

/* the token should be related to pointer type. */
struct variable {
    struct ptr_info ptr_info;

    union {
        /*
         * The @object in structure is same as this.
         * So we can use vairable->object.type to determine
         * the variable is structure or not.
         */
        struct object object;
        struct structure struct_info;
    };

    // TODO: Simplify the init function
    union {
        /* Default (scope) variable */
        struct list_head scope_node;

        /* struct member */
        struct list_head struct_node;

        /* For the function only */
        struct list_head parameter_node;
    };
};

// TODO: hadnle this like the structure (support scope definition, etc)
struct typedef_info_node {
    struct symbol *new_type_symbol;
    // file,line,offset,buffer info
    struct list_head node;
};

struct typedef_info {
    struct object orig_object;
    struct list_head head;

    /* sfc->typedef_info_head */
    struct list_head node;
};

struct union_info {
    // TODO
};

struct scope {
    struct list_head func_scope_node;
    struct list_head scope_var_head;
};

struct function {
    /* If it is function declaration the func_scope_head is empty. */
    struct list_head func_scope_head;

    /* function info */
    struct object object;
    struct list_head parameter_head;

    int nr_state;
    struct list_head state_head;

    /* For struct file_info */
    struct list_head node;
};

struct file_info {
    /* e.g., generated_test_name.c */
    char generated_name[MAX_NR_GENERATED_NAME];
    /* e.g., tests/test_name.c */
    char full_name[MAX_NR_NAME];
    /* e.g., test_name.c */
    char name[MAX_NR_NAME];
    FILE *file;
    /* For osc data struct in src/osc.c */
    struct list_head node;

    struct list_head func_head;
    struct list_head struct_head;
};

struct scan_file_control {
    struct file_info *fi;

    char name[MAX_NR_GENERATED_NAME];

    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned long line;

    struct /* peak token info */ {
        unsigned int peak;
        struct list_head peak_head;
    };

    struct list_head typedef_info_head;

    /* for the if statement (control dependence). */
    struct function *function;
    struct function *real_function;
};

struct scope_iter_data {
    struct scope *scope;
    struct variable *var;
};

struct function_state {
    int id;
    struct function function;
    struct list_head state_node;
};

#define for_each_var_in_scopes(func, iter)                                \
    list_for_each_entry ((iter)->scope, &func->func_scope_head,           \
                         func_scope_node)                                 \
        list_for_each_entry ((iter)->var, &(iter)->scope->scope_var_head, \
                             scope_node)

#define for_each_var(scope, var) \
    list_for_each_entry (var, &scope->scope_var_head, scope_node)

int parser(struct file_info *fi);

#endif /* __OSC_PARSER_H__ */
