#include "../include/uapi/ownership.h"

void set_and_drop(int __mut *ptr)
{
    //*ptr = 1;

    if (1) {
        // case 1: the drop the variable
        release(ptr);
    } else {
        *ptr = 2;
    }
    
    *ptr = 1;
    // warning: write to dropped object
}

void set_and_set(void)
{
    int a = 0;
    int __mut *ptr = &a;

    if (1) {
        int b = 2;
        ptr = &b;
    }

    // warning: should drop: set at ptr = &b
}

void drop_and_set(int __mut *ptr)
{

    release(ptr);

    if (1) {
        int a = 1;
        ptr = &a;
    }

    // warning: should drop: set at ptr = &a
}

void if_else_if(int __mut *ptr)
{
    if (1) {
        int a = 1;
        *ptr = 1;
    } else if (1) {
        int a = 1;
        release(ptr);
    } else if (1) {
        int a= 1;
    } else {
        int a = 1;
    }

    *ptr = 1;
    // warning: potentially drop the variable
}
