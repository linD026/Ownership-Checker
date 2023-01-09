#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <osc/debug.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define MAX_NR_NAME 80

struct symbol {
    char *name;
    unsigned int len;
    int flags;
};

#define __SYM_ENTRY(_name, _flags) \
    [_flags] = { .name = #_name, .len = sizeof(#_name) - 1, .flags = _flags }

#define SYM_ENTRY(_name) __SYM_ENTRY(_name, sym_##_name)

struct object {
    int storage_class;
    int type;
    int is_struct;
    struct symbol *struct_id;
    int is_ptr;
    int attr;
    struct symbol *id;
};

/* the token should be related to pointer type. */
struct variable {
    struct object object;
    struct list_head scope_node;
    struct list_head structure_node;
    struct list_head func_scope_node;

    /* For the function only */
    struct list_head parameter_node;
};

struct scope {
    struct list_head node;
    struct list_head scope_var_head;
};

struct structure {
    /* variable */
    struct list_head var_head;

    /* For struct file_info */
    struct list_head node;
};

struct function {
    struct list_head func_scope_var_head;
    /* If it is function declaration the func_scope_head is empty. */
    struct list_head func_scope_head;

    /* function info */
    struct object object;
    struct list_head parameter_var_head;

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

#define MAX_BUFFER_LEN 128

struct scan_file_control {
    struct file_info *fi;
    char buffer[MAX_BUFFER_LEN];
    unsigned int size;
    unsigned int offset;
    unsigned long line;

    struct function *function;
};

int parser(struct file_info *fi);

static __always_inline void bad(struct scan_file_control *sfc,
                                const char *warning)
{
    print("\e[1m\e[31mOSC ERROR\e[0m\e[0m: \e[1m%s\e[0m\n", warning);
    print("    \e[36m-->\e[0m %s:%lu:%u\n", sfc->fi->name, sfc->line,
          sfc->offset);
    print("    \e[36m|\e[0m    %s", sfc->buffer);
    print("         ");
    for (int i = 0; i < sfc->offset; i++)
        print(" ");
    print("\e[31m^\e[0m\n");
}

#define syntax_error(sfc) bad(sfc, "syntax error")

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

#define for_each_line(sfc)                   \
    while ((sfc)->offset = 0, (sfc)->line++, \
           fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL)

#define next_line(sfc)                                                    \
    ({                                                                    \
        (sfc)->offset = 0;                                                \
        (sfc)->line++,                                                    \
            (fgets((sfc)->buffer, (sfc)->size, (sfc)->fi->file) != NULL); \
    })

/*
 * We don't check the terminal symbol '\0', since we should make sure that
 * each time we iterate the buffer we won't skip any symbol.
 * But for some times (i.e., debugging), we can add this.
 * (sfc)->buffer[(sfc)->offset] == '\0' ||\
 */
#define line_end(sfc) \
    ((sfc)->offset >= (sfc)->size || (sfc)->buffer[(sfc)->offset] == '\n')

#define buffer_for_each(sfc)                                     \
    for (char ch = (sfc)->buffer[(sfc)->offset]; !line_end(sfc); \
         ch = (sfc)->buffer[++(sfc)->offset])

#define buffer_rest(sfc) ((sfc)->size - (sfc)->offset + 1)

/*
 * It will get the next non-blank symbols. it's same as following function:
 *
 *   static __always_inline int decode_obj_action(
 *       struct scan_file_control *sfc, struct object_struct *obj,
 *       int (*action)(struct scan_file_control *, struct object_struct *))
 *   {
 *       if (!action)
 *           return -EINVAL;
 *   again:
 *       buffer_for_each (sfc) {
 *           if (blank(ch))
 *               continue;
 *           if (!action(sfc, obj))
 *               return 0;
 *       }
 *       next_line(sfc);
 *       goto again;
 *   }
 *
 */
#define ___decode_action(sfc, action, id, ret, ...) \
    do {                                            \
        da_##id##_again : buffer_for_each (sfc)     \
        {                                           \
            if (blank(ch))                          \
                continue;                           \
            ret = action(sfc, ##__VA_ARGS__);       \
            if (ret != -EAGAIN)                     \
                goto da_##id##_out;                 \
        }                                           \
        if (next_line(sfc))                         \
            goto da_##id##_again;                   \
        else                                        \
            ret = -ENODATA;                         \
        da_##id##_out:;                             \
    } while (0)

#define __decode_action(sfc, action, id, ...)                                \
    ({                                                                       \
        int __da_ret = 0;                                                    \
        ___decode_action(sfc, action, id, __da_ret, ##__VA_ARGS__);          \
        WARN_ON(__da_ret < 0 && __da_ret != -EAGAIN,                         \
                "action:%s, error:%s, buf:%s", #action, strerror(-__da_ret), \
                &sfc->buffer[sfc->offset]);                                  \
        __da_ret;                                                            \
    })

#define decode_action(sfc, action, ...) \
    __decode_action(sfc, action, __LINE__, ##__VA_ARGS__)

#define __try_decode_action(sfc, action, id, ...)                   \
    ({                                                              \
        int __da_ret = 0;                                           \
        ___decode_action(sfc, action, id, __da_ret, ##__VA_ARGS__); \
        __da_ret;                                                   \
    })

#define try_decode_action(sfc, action, ...) \
    __try_decode_action(sfc, action, __LINE__, ##__VA_ARGS__)

/* Token */

enum {
    sym_dump = -1,

    /* sym_table start */

    /* attr start - brw */
    sym_attr_brw,
    sym_attr_mut,
    sym_attr_clone,
    /* attr end - clone */

    /* sym storage class start - auto */
    sym_auto,
    sym_register,
    sym_static,
    sym_extern,
    /* sym storage class end - extern */

    /* sym type start - int */
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

    sym_malloc,
    sym_free,

    /* sym id start - ptr_assign */
    sym_ptr_assign, // ->
    sym_logic_or, // ||
    sym_logic_and, // &&

    /* sym_table end */

    /* sym one char start - left_paren */
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
    sym_comma, // ,
    sym_dot, // .
    sym_seq_point, // ;
    /* sym one char end - seq point */
    /* sym id end - seq point */

    /* constant */
    sym_numeric_constant,

    /* id */
    sym_id,
};

#define sym_attr_start sym_attr_brw
#define sym_attr_end sym_attr_clone

#define sym_storage_class_start sym_auto
#define sym_storage_class_end sym_extern

#define sym_type_start sym_int
#define sym_type_end sym_void

#define sym_one_char_start sym_left_paren
#define sym_one_char_end sym_seq_point

/* Decode the id until the symbol is between sym_id_start to sym_id_end. */
#define sym_id_start sym_ptr_assign
#define sym_id_end sym_seq_point

#define range_in_sym(range_name, number) \
    (sym_##range_name##_start <= number && number <= sym_##range_name##_end)

void symbol_id_container_release(void);
int get_token(struct scan_file_control *sfc, struct symbol **id);
int cmp_token(struct symbol *l, struct symbol *r);
const char *token_name(int n);

static __always_inline char debug_sym_one_char(int sym)
{
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
    case sym_comma:
        return ',';
    case sym_dot:
        return '.';
    case sym_seq_point:
        return ';';
    case -ENODATA:
        return -ENODATA;
    default:
        WARN_ON(1, "failed:%d", sym);
        return sym;
    }
}

#define debug_token(sfc, _sym, _symbol)                                       \
    do {                                                                      \
        if (_symbol == NULL) {                                                \
            int __c = debug_sym_one_char(_sym);                               \
            if (__c != -ENODATA)                                              \
                pr_info("char          : |%c|\n", (char)__c);                 \
        } else                                                                \
            pr_info("symbol(%2d, %2u): |%s|\n", _symbol->flags, _symbol->len, \
                    _symbol->name);                                           \
    } while (0)

#endif /* __OSC_PARSER_H__ */
