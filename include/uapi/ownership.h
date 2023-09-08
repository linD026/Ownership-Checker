#ifndef __OWNERSHIP_H__
#define __OWNERSHIP_H__

#define __immutable /* Default */
#define __mut
#define __brw

#define NOTE_ALLOCATION(struct_name, alloc_function, release_function)

#define ALL_TYPE /* int, float, void, ... */
NOTE_ALLOCATION(ALL_TYPE, malloc, free)

#define __clone
#define clone_to(clone_function, src)

/*
 * Two types of release:
 * - release(ptr)
 *      - dummy release, do nothing
 * - release(type, ptr)
 *      - real release
 */
#define release(...) \
    __RELEASE_ARGS(__VA_ARGS__, __RELEASE_REAL, __RELEASE_DUMMY)(__VA_ARGS__)

#define __RELEASE_ARGS(_DUMMY_1, _DUMMY_2, release_function, ...) \
    release_function
#define __RELEASE_DUMMY(ptr, ...)
#define __RELEASE_REAL(type, ptr) release_##type(ptr)

#endif /* __OWNERSHIP_H__ */
