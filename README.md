# Ownership Checker for C Language

## Build

```bash
make                # Build the program
make verbose=1      # Debug mode
make clean          # Delete generated files
```

## Example

```
$ make
$ ./osc tests/test_write.c 
OSC Analyzes file: tests/test_write.c
OSC ERROR: Return the dropped object
    |-> tests/test_write.c:4:13
    |        return (b + 1);
    |                ^
    +-> Dropped at tests/test_write.c:3:18
    |        function2(a, b, c);
    |                     ^
OSC NOTE: The object is declared as function2's argument: int __mut *b
OSC ERROR: Don't write to the borrowed object
    |-> tests/test_write.c:11:14
    |        a = borrow = 3;
    |                 ^
OSC NOTE: The object is declared as function's argument: int __brw *borrow
OSC ERROR: Return the dropped object
    |-> tests/test_write.c:15:18
    |        return mutable;
    |                     ^
    +-> Dropped at tests/test_write.c:13:24
    |        function2(a, mutable, borrow);
    |                           ^
OSC NOTE: The object is declared as function's argument: int __mut *mutable
```

Now, we only check the ownership of the pointer that has the `__mut` or `__brw` attribute.

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
