#ifndef __OSC_DEBUG_H__
#define __OSC_DEBUG_H__

#include <osc/compiler.h>

#if defined(CONFIG_DEBUG)

#include <osc/print.h>
#include <stdlib.h>
#include <execinfo.h>

#define STACK_BUF_SIZE 32

static __always_inline void dump_stack(void)
{
    char **stack_info;
    int nr = 0;
    void *buf[STACK_BUF_SIZE];

    nr = backtrace(buf, STACK_BUF_SIZE);
    stack_info = backtrace_symbols(buf, nr);

    print("========== dump stack start ==========\n");
    for (int i = 0; i < nr; i++)
        print("  %s\n", stack_info[i]);
    print("========== dump stack  end  ==========\n");
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
    do {                                                           \
        if (unlikely(cond))                                        \
            pr_err("WARN ON:" #cond ", " fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define pr_debug(fmt, ...) pr_info("DEBUG: " fmt, ##__VA_ARGS__)

#else /* ndef CONFIG_DEBUG */

static __always_inline void dump_stack(void);
#define BUG_ON(cond, fmt, ...) \
    do {                       \
    } while (0)
#define WARN_ON(cond, fmt, ...) \
    do {                        \
    } while (0)
#define pr_debug(fmt, ...) \
    do {                   \
    } while (0)

#endif /* ifdef CONFIG_DEBUG */

#endif /* __OSC_DEBUG_H__ */
