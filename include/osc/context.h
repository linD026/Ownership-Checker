#ifndef __OSC_CONTEXT_H__
#define __OSC_CONTEXT_H__

#include <osc/arena.h>
#include <osc/list.h>
#include <stdio.h>

struct file_info;
struct scan_file_control;
struct ast_translation_unit;

enum osc_mode {
    OSC_MODE_AUTO = 0,
    OSC_MODE_KERNEL,
    OSC_MODE_OSC,
};

struct osc_context {
    struct file_info *fi;
    struct scan_file_control *sfc;
    struct osc_arena arena;
    enum osc_mode mode;
    struct ast_translation_unit *tu;

    unsigned int error_count;
    unsigned int warning_count;

    struct list_head node;
};

void osc_context_init(struct osc_context *ctx, struct file_info *fi,
                      enum osc_mode mode);
void osc_context_destroy(struct osc_context *ctx);

#endif /* __OSC_CONTEXT_H__ */
