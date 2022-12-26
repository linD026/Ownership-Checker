#ifndef __OSC_PRINT_H__
#define __OSC_PRINT_H__

#include <stdio.h>
#include <sys/time.h>

#define debug_stream stdout
#define err_stream stderr

static inline unsigned long __get_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (unsigned long)1000000 * tv.tv_sec + tv.tv_usec;
}

#define print(fmt, ...)                            \
    do {                                           \
        fprintf(debug_stream, fmt, ##__VA_ARGS__); \
    } while (0)

#define pr_info(fmt, ...)                                                 \
    do {                                                                  \
        print("\e[32m[%-10lu]\e[0m %s:%d:%s: " fmt, __get_ms(), __FILE__, \
              __LINE__, __func__, ##__VA_ARGS__);                         \
    } while (0)

#define pr_err(fmt, ...)                                                  \
    do {                                                                  \
        fprintf(err_stream,                                               \
                "\e[32m[%-10lu]\e[0m %s:%d:%s: "                          \
                "\e[31m" fmt "\e[0m",                                     \
                __get_ms(), __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

#endif /* __OSC_PRINT_H__ */
