# Ownership Checker for C Language

WARNING:
Now, this project is restructuring, you may get the following example by
rebasing to this commit ff934ed.

## Build

```bash
make                # Build the program
make clean          # Delete generated files
```

## Example

```
$ make debug=1
$ ./osc tests/test_function_definition.c 
OSC Analyzes file: tests/test_function_definition.c
OSC ERROR: Don't write to immutable object
    --> tests/test_function_definition.c:3:7
    |        a = 1;
    | 
    = ==== dump object ====
    || function; func
    || type; int  
    || name: a
    || info: fso_function_args
    = =====================
OSC ERROR: Don't write to borrowed object
    --> tests/test_function_definition.c:5:8
    |        *c = 3;
    | 
    = ==== dump object ====
    || function; func
    || type; int __brw *
    || name: c
    || info: fso_function_args
    = =====================
OSC ERROR: Don't write to immutable object
    --> tests/test_function_definition.c:9:11
    |            a = 1;
    | 
    = ==== dump object ====
    || function; func
    || type; int  
    || name: a
    || info: fso_function_args
    = =====================
OSC ERROR: Don't write to immutable object
    --> tests/test_function_definition.c:15:7
    |        a = 1;
    | 
    = ==== dump object ====
    || function; func2
    || type; int  
    || name: a
    || info: fso_function_args
    = =====================
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
