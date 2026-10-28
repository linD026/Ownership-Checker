#include <osc/arena.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    struct osc_arena arena;
    char *p1 = NULL;
    char *p3 = NULL;
    void *p2 = NULL;
    void *large = NULL;
    int *arr = NULL;
    int i = 0;

    osc_arena_init(&arena, 1024);
    assert(list_empty(&arena.chunks));
    assert(arena.total_allocated == 0);

    /* 1. Basic allocation */
    p1 = osc_arena_alloc(&arena, 64);
    assert(p1 != NULL);
    assert(!list_empty(&arena.chunks));
    strcpy(p1, "hello arena");
    assert(strcmp(p1, "hello arena") == 0);

    /* 2. Alignment test */
    p2 = osc_arena_alloc_align(&arena, 12, 64);
    assert(p2 != NULL);
    assert(((uintptr_t)p2 % 64) == 0);

    /* 3. Zero-allocation */
    arr = osc_arena_zalloc(&arena, sizeof(int) * 10);
    assert(arr != NULL);
    for (i = 0; i < 10; i++)
        assert(arr[i] == 0);

    /* 4. Large allocation exceeding chunk size */
    large = osc_arena_alloc(&arena, 4096);
    assert(large != NULL);

    /* 5. Reset */
    osc_arena_reset(&arena);
    assert(arena.total_allocated == 0);

    /* Allocate again after reset */
    p3 = osc_arena_alloc(&arena, 32);
    assert(p3 != NULL);

    /* 6. Destroy */
    osc_arena_destroy(&arena);
    assert(list_empty(&arena.chunks));
    assert(arena.total_allocated == 0);

    printf("Arena unit tests passed!\n");
    return 0;
}
