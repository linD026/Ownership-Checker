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

#endif /* __OWNERSHIP_H__ */
