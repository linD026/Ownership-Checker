#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <osc/debug.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define MAX_BUFFER_LEN 128
#define MAX_NR_NAME 80

struct symbol {
    char *name;
    unsigned int len;
    int flags;
};

#define ATTR_FLAGS_BRW 0x0001
#define ATTR_FLAGS_CLONE 0x0002
#define ATTR_FLAGS_MUT 0x0004
#define ATTR_FLAS_MASK (ATTR_FLAGS_BRW | ATTR_FLAGS_CLONE | ATTR_FLAGS_MUT)

struct object {
    int storage_class;
    int type;
    // TODO: use the counter
    int is_ptr;
    int attr;
    struct symbol *struct_id;
    struct symbol *id;
};

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
    char name[MAX_NR_NAME];
    FILE *file;
    /* For osc data struct in src/osc.c */
    struct list_head node;

    struct list_head func_head;
    struct list_head struct_head;
};

struct scan_file_control {
    struct file_info *fi;
    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned long line;

    struct /* peak token info */ {
        unsigned int peak;
        struct list_head peak_head;
    };

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

static __always_inline int blank(char ch)
{
    switch (ch) {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    case '\r':
        return 1;
    }
    return 0;
}

static __always_inline int bad_get_last_offset(const char *buffer,
                                               unsigned int offset)
{
    /*
     * we manually get the offset of last id symbol.
     * Otherwise, if we use sfc->offset to get the lcoation it
     * will be the first offset of next symbol
     */
    for (unsigned int i = offset; i >= 0; i--) {
        if (blank(buffer[i]))
            continue;
        /* See the symbol table - one char */
        switch (buffer[i]) {
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case '*':
        case '<':
        case '>':
        case '=':
        case '+':
        case '-':
        case ',':
        case '.':
        case ';':
            break;
        default:
            return i;
        }
    }
    WARN_ON(1, "cannot get last symbol's offset");
    return -1;
}

static __always_inline void bad_template(int level, const char *file,
                                         unsigned int line, const char *buffer,
                                         unsigned int offset, const char *note,
                                         const char *warning)
{
    int last_local = bad_get_last_offset(buffer, offset);
    char level_symbol = (level) ? '+' : '|';

    if (warning)
        print("\e[1m\e[31mOSC ERROR\e[0m\e[0m: \e[1m%s\e[0m\n", warning);

    if (note) {
        print("    \e[36m%c->\e[0m %s %s:%u:%u\n", level_symbol, note, file,
              line, last_local + 1);
    } else {
        print("    \e[36m%c->\e[0m %s:%u:%u\n", level_symbol, file, line,
              last_local + 1);
    }

    print("    \e[36m|\e[0m    %s", buffer);
    print("    \e[36m|\e[0m    ");
    for (int i = 0; i < last_local; i++)
        print(" ");
    print("\e[31m^\e[0m\n");
}

static __always_inline void bad(struct scan_file_control *sfc,
                                const char *warning)
{
    bad_template(0, sfc->fi->name, sfc->line, sfc->buffer, sfc->offset, NULL,
                 warning);
}

#define syntax_error(sfc) bad(sfc, "syntax error")

#define bad_on_ptr_info(sfc, info, note)                         \
    bad_template(1, sfc->fi->name, (info)->line, (info)->buffer, \
                 (info)->offset, note, NULL)

#define bad_on_dropped_info(sfc, dropped_info) \
    bad_on_ptr_info(sfc, dropped_info, "Dropped at")

#define bad_on_set_info(sfc, set_info) bad_on_ptr_info(sfc, set_info, "Set at")

#ifdef CONFIG_DEBUG
#define debug_ptr_info(info, note)                                  \
    bad_template(0, "debug_ptr_info", (info)->line, (info)->buffer, \
                 (info)->offset, note, NULL)
#else
#define debug_ptr_info(...)
#endif /* CONFIG_DEBUG */

#define for_each_line(sfc)                   \
    while ((sfc)->offset = 0, (sfc)->line++, \
           fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL)

#define next_line(sfc)                                                    \
    ({                                                                    \
        int ret =                                                         \
            (fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL); \
        if (ret) {                                                        \
            (sfc)->offset = 0;                                            \
            (sfc)->line++;                                                \
        }                                                                 \
        ret;                                                              \
    })

/* Token */

enum {
    sym_dump = -1,

/* sym_table start */

/* attr start - brw */
#define sym_attr_start sym_attr_brw
    sym_attr_brw,
    sym_attr_mut,
    sym_attr_clone,
#define sym_attr_end sym_attr_clone
/* attr end - clone */

/* sym storage class start - auto */
#define sym_storage_class_start sym_auto
    sym_auto,
    sym_register,
    sym_static,
    sym_extern,
#define sym_storage_class_end sym_extern
/* sym storage class end - extern */

/* sym qualifier class start - const */
#define sym_qualifier_start sym_const
    sym_const,
    sym_volatile,
    sym_restrict,
    sym__Atomic,
#define sym_qualifier_end sym__Atomic
/* sym qualifier class end - _Atomic */

/* sym type start - int */
#define sym_type_start sym_int
    sym_int,
    sym_short,
    sym_long,
    sym_long_long,
    sym_unsigned_int,
    sym_unsigned_short,
    sym_unsigned_long,
    sym_unsigned_long_long,
    sym_char,
    sym_signed_char,
    sym_unsigned_char,
    sym_double,
    sym_long_double,
    sym_float,
    sym_struct,
    sym_void,
#define sym_type_end sym_void
    /* sym type end - void */

    /* other multiple char keywords */
    sym_do,
    sym_while,
    sym_for,
    sym_if,
    sym_else,
    sym_switch,
    sym_case,
    sym_return,
    sym_true, /* C23 keyword true, false */
    sym_false,

    /* preprocessor keywords */
    sym_include,
    sym_define,

/* Decode the id until the symbol is between sym_id_start to sym_id_end. */
/* sym id start - ptr_assign */
#define sym_id_start sym_ptr_assign
    sym_ptr_assign, // ->
    sym_logic_or, // ||
    sym_logic_and, // &&
    sym_equal, // ==

/* sym_table end */

/* sym one char start - left_paren */
#define sym_one_char_start sym_left_paren
    sym_left_paren, // ()
    sym_right_paren,
    sym_left_brace, // {}
    sym_right_brace,
    sym_left_sq_brace, // []
    sym_right_sq_brace,
    sym_aster, // *
    sym_lt, // <
    sym_gt, // >
    sym_eq, // =
    sym_add, // +
    sym_minus, // -
    sym_quotation, // "
    sym_bit_and, // &
    sym_bit_or, // |
    sym_comma, // ,
    sym_dot, // .
    sym_seq_point, // ;
#define sym_one_char_end sym_seq_point
/* sym one char end - seq point */
#define sym_id_end sym_seq_point
    /* sym id end - seq point */

    /* constant */
    sym_numeric_constant,
    sym_string_literals,

    /* id */
    sym_id,
};

#define range_in_sym(range_name, number) \
    (sym_##range_name##_start <= number && number <= sym_##range_name##_end)

void symbol_id_container_release(void);
int get_token(struct scan_file_control *sfc, struct symbol **id);
int cmp_token(struct symbol *l, struct symbol *r);
const char *token_name(int n);

// TODO: We should auto generate these...
static __always_inline char debug_sym_one_char(int sym)
{
#ifdef CONFIG_DEBUG
    switch (sym) {
    case sym_left_paren:
        return '(';
    case sym_right_paren:
        return ')';
    case sym_left_brace:
        return '{';
    case sym_right_brace:
        return '}';
    case sym_left_sq_brace:
        return '[';
    case sym_right_sq_brace:
        return ']';
    case sym_aster:
        return '*';
    case sym_lt:
        return '<';
    case sym_gt:
        return '>';
    case sym_eq:
        return '=';
    case sym_add:
        return '+';
    case sym_minus:
        return '-';
    case sym_comma:
        return ',';
    case sym_quotation:
        return '"';
    case sym_bit_and:
        return '&';
    case sym_bit_or:
        return '|';
    case sym_dot:
        return '.';
    case sym_seq_point:
        return ';';
    case -ENODATA:
        return -ENODATA;
    default:
        /* We might have single char id, so just return it. */
        return sym;
    }
#endif /* CONFIG_DEBUG */
    return sym;
}

static __allow_unused void __debug_token(struct scan_file_control *sfc, int sym,
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

#ifdef CONFIG_DEBUG
#define debug_token(sfc, sym, symbol)    \
    do {                                 \
        pr_info(" ");                    \
        __debug_token(sfc, sym, symbol); \
    } while (0)
#else
#define debug_token(sfc, sym, symbol) \
    do {                              \
    } while (0)
#endif /* CONFIG_DEBUG */

struct symbol *new_anon_symbol(void);

int peak_token(struct scan_file_control *sfc, struct symbol **id);

static inline void flush_peak_token(struct scan_file_control *sfc)
{
    struct symbol *symbol;
    get_token(sfc, &symbol);
}

#endif /* __OSC_PARSER_H__ */
