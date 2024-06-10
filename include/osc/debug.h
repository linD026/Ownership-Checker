#ifndef __OSC_DEBUG_H__
#define __OSC_DEBUG_H__

#include <osc/compiler.h>
#include <osc/token.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <errno.h>

#define debug_stream stdout
#define err_stream stderr

#define print(fmt, ...)                            \
    do {                                           \
        fprintf(debug_stream, fmt, ##__VA_ARGS__); \
    } while (0)

#define pr_info(fmt, ...)                                             \
    do {                                                              \
        print("\e[32m[INFO]\e[0m %s:%d:%s: " fmt, __FILE__, __LINE__, \
              __func__, ##__VA_ARGS__);                               \
    } while (0)

#define pr_err(fmt, ...)                                      \
    do {                                                      \
        fprintf(err_stream,                                   \
                "\e[32m[ERROR]\e[0m %s:%d:%s: "               \
                "\e[31m" fmt "\e[0m",                         \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

static __always_inline __allow_unused void dump_stack(void)
{
#define STACK_BUF_SIZE 32
    char **stack_info;
    int nr = 0;
    void *buf[STACK_BUF_SIZE];

    nr = backtrace(buf, STACK_BUF_SIZE);
    stack_info = backtrace_symbols(buf, nr);

    print("========== dump stack start ==========\n");
    for (int i = 0; i < nr; i++)
        print("  %s\n", stack_info[i]);
    print("========== dump stack  end  ==========\n");
#undef STACK_BUF_SIZE
}

#define BUG_ON(cond, fmt, ...)                                     \
    do {                                                           \
        if (unlikely(cond)) {                                      \
            pr_err("BUG ON: " #cond ", " fmt "\n", ##__VA_ARGS__); \
            dump_stack();                                          \
            exit(EXIT_FAILURE);                                    \
        }                                                          \
    } while (0)

#define WARN_ON(cond, fmt, ...)                                    \
    ({                                                             \
        int __w_i_c_d = (cond);                                    \
        if (unlikely(__w_i_c_d))                                   \
            pr_err("WARN ON:" #cond ", " fmt "\n", ##__VA_ARGS__); \
        __w_i_c_d;                                                 \
    })

#ifdef CONFIG_DEBUG
#define pr_debug(fmt, ...) pr_info("DEBUG: " fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)
#endif

/* Token */

// TODO where should this place?
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

#define bad(sfc, warning)                                                 \
    bad_template(0, sfc->name, sfc->line, sfc->buffer, sfc->offset, NULL, \
                 warning)

#define syntax_error(sfc) bad(sfc, "syntax error")

#define bad_on_ptr_info(sfc, info, note)                                     \
    bad_template(1, sfc->name, (info)->line, (info)->buffer, (info)->offset, \
                 note, NULL)

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

/* token */

struct scan_file_control;
struct symbol;

__allow_unused void __debug_token(struct scan_file_control *sfc, int sym,
                                  struct symbol *symbol);

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

/* object */

struct object;
struct variable;
struct structure;
struct function;

void debug_object(struct object *obj, const char *note);
void debug_variable(struct variable *var, const char *note);
void debug_structure(struct structure *structure, const char *note);
void debug_function(struct function *function);

#endif /* __OSC_DEBUG_H__ */
