#ifndef __OSC_DEBUG_H__
#define __OSC_DEBUG_H__

#include <osc/compiler.h>
#include <stdio.h>

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

#include <stdlib.h>
#include <execinfo.h>

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

#endif /* __OSC_DEBUG_H__ */
