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
OSC Analyzes file: tests/test_write.c
OSC ERROR: Return the dropped object
    |-> tests/test_write.c:4:13
    |        return (b + 1);
    |                ^
    +-> Dropped at tests/test_write.c:3:18
    |        function2(a, b, c);
    |                     ^
OSC NOTE: The object is declared as function2 argument: int __mut *b
OSC ERROR: Don't write to the borrowed object
    |-> tests/test_write.c:11:15
    |        a = *borrow = 3;
    |                  ^
OSC NOTE: The object is declared as function argument: int __brw *borrow
OSC ERROR: Should release the end-of-life object
    |-> tests/test_write.c:17:6
    |        }
    |         ^
    +-> Set at tests/test_write.c:16:29
    |            int __mut *scoped_ptr = &a;
    |                                ^
OSC NOTE: The object is declared as function scope: int __mut *scoped_ptr
OSC ERROR: Return the dropped object
    |-> tests/test_write.c:19:18
    |        return mutable;
    |                     ^
    +-> Dropped at tests/test_write.c:13:24
    |        function2(a, mutable, borrow);
    |                           ^
OSC NOTE: The object is declared as function argument: int __mut *mutable
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
