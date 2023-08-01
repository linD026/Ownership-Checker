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
$ ./osc tests/test_function_definition.c 
OSC Analyzes file: tests/test_function_definition.c
OSC ERROR: Don't write to the borrowed object
    --> tests/test_function_definition.c:8:8
    |        *c = 3;
                  ^
OSC NOTE: The object is declared as func's argument: int __brw *c
OSC ERROR: Don't write to the borrowed object
    --> tests/test_function_definition.c:9:8
    |        *d = 4;
                  ^
OSC NOTE: The object is declared as func's argument: int __brw *d
OSC ERROR: Don't write to the dropped object
    --> tests/test_function_definition.c:22:7
    |        b = 1;
                 ^
OSC NOTE: The object is declared as func2's argument: int __mut b
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
