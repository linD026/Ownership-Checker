#ifndef __OSC_ARENA_H__
#define __OSC_ARENA_H__

#include <stddef.h>
#include <osc/list.h>

#define OSC_ARENA_DEFAULT_CHUNK_SIZE (64 * 1024)

struct osc_arena_chunk {
    struct list_head node;
    size_t capacity;
    size_t used;
    char data[] __attribute__((aligned(sizeof(void *))));
};

struct osc_arena {
    struct list_head chunks;
    size_t chunk_size;
    size_t total_allocated;
};

void osc_arena_init(struct osc_arena *arena, size_t chunk_size);
void *osc_arena_alloc(struct osc_arena *arena, size_t size);
void *osc_arena_alloc_align(struct osc_arena *arena, size_t size, size_t align);
void *osc_arena_zalloc(struct osc_arena *arena, size_t size);
void osc_arena_reset(struct osc_arena *arena);
void osc_arena_destroy(struct osc_arena *arena);

#endif /* __OSC_ARENA_H__ */
