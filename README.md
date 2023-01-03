# Ownership Checker for C Language

## Build

```bash
make                # Build the program
make clean          # Delete generated files
```

## Sample

```cpp
// Warn on the pure pointer type in function prototype, i.e. `int *p` .
int function(int *move, int __clone *clone, int copy)
{
    int *objA;
    int /* __heap */  *objB_from_heap = malloc(); // obj, allocate memory from heap
    
    // change ownership
    objA = objB_from_heap;
    
    // end of scope
    // - end of lifetime: move, clone, copy
    return *objA;
    // pass value, don't have to consider the ownership
}
// Warning: don't have free() for obj, move, and clone
```

```cpp
int *funcA(const int /* __immutable */ *imt, int __mut *mut, int __brw *brw)
{
    ...
}
// Warning: don't have free() for imt, and mut
```
