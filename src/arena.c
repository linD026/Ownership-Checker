#include <osc/arena.h>
#include <osc/debug.h>
#include <osc/compiler.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ALIGNMENT sizeof(void *)

static inline size_t align_up(size_t n, size_t align)
{
    return (n + align - 1) & ~(align - 1);
}

static struct osc_arena_chunk *new_chunk(size_t capacity)
{
    size_t total = sizeof(struct osc_arena_chunk) + capacity;
    struct osc_arena_chunk *chunk = malloc(total);

    BUG_ON(!chunk, "malloc arena chunk failed");

    list_init(&chunk->node);
    chunk->capacity = capacity;
    chunk->used = 0;

    return chunk;
}

void osc_arena_init(struct osc_arena *arena, size_t chunk_size)
{
    if (chunk_size == 0)
        chunk_size = OSC_ARENA_DEFAULT_CHUNK_SIZE;

    list_init(&arena->chunks);
    arena->chunk_size = chunk_size;
    arena->total_allocated = 0;
}

void *osc_arena_alloc_align(struct osc_arena *arena, size_t size, size_t align)
{
    struct osc_arena_chunk *curr_chunk = NULL;
    struct osc_arena_chunk *chunk = NULL;
    size_t aligned_offset = 0;
    size_t new_cap = 0;
    void *ptr = NULL;

    if (unlikely(size == 0))
        return NULL;

    if (unlikely(align == 0 || (align & (align - 1)) != 0))
        align = DEFAULT_ALIGNMENT;

    if (!list_empty(&arena->chunks)) {
        curr_chunk =
            list_last_entry(&arena->chunks, struct osc_arena_chunk, node);
        aligned_offset = align_up(curr_chunk->used, align);

        if (aligned_offset + size <= curr_chunk->capacity) {
            ptr = curr_chunk->data + aligned_offset;
            curr_chunk->used = aligned_offset + size;
            arena->total_allocated += size;
            return ptr;
        }
    }

    /* Need a new chunk: allocate max of default chunk_size or needed size */
    new_cap = max(arena->chunk_size, size + align);
    chunk = new_chunk(new_cap);

    list_add_tail(&chunk->node, &arena->chunks);

    aligned_offset = align_up(0, align);
    ptr = chunk->data + aligned_offset;
    chunk->used = aligned_offset + size;
    arena->total_allocated += size;

    return ptr;
}

void *osc_arena_alloc(struct osc_arena *arena, size_t size)
{
    return osc_arena_alloc_align(arena, size, DEFAULT_ALIGNMENT);
}

void *osc_arena_zalloc(struct osc_arena *arena, size_t size)
{
    void *ptr = osc_arena_alloc(arena, size);

    if (likely(ptr))
        memset(ptr, 0, size);

    return ptr;
}

void osc_arena_reset(struct osc_arena *arena)
{
    struct osc_arena_chunk *chunk = NULL;
    struct osc_arena_chunk *tmp = NULL;

    list_for_each_entry_safe(chunk, tmp, &arena->chunks, node)
    {
        chunk->used = 0;
    }
    arena->total_allocated = 0;
}

void osc_arena_destroy(struct osc_arena *arena)
{
    struct osc_arena_chunk *chunk = NULL;
    struct osc_arena_chunk *tmp = NULL;

    list_for_each_entry_safe(chunk, tmp, &arena->chunks, node)
    {
        list_del(&chunk->node);
        free(chunk);
    }

    arena->total_allocated = 0;
    list_init(&arena->chunks);
}
