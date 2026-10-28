#include <osc/context.h>
#include <osc/debug.h>
#include <string.h>

void osc_context_init(struct osc_context *ctx, struct file_info *fi,
                      enum osc_mode mode)
{
    BUG_ON(!ctx, "null context pointer");

    memset(ctx, 0, sizeof(*ctx));
    ctx->fi = fi;
    ctx->mode = mode;
    ctx->sfc = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;

    list_init(&ctx->node);
    osc_arena_init(&ctx->arena, 0);
}

void osc_context_destroy(struct osc_context *ctx)
{
    if (unlikely(!ctx))
        return;

    osc_arena_destroy(&ctx->arena);
    ctx->fi = NULL;
    ctx->sfc = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
}
