#include <stdio.h>

void file_functionA(void)
{
    int a = 1;
}

#include "../include/uapi/ownership.h"

void file_functionB(void)
{
    int a = 1;
}

#define macro_definition_A(a, b) \
    do {\
        a = b;\
    } while (0)

void file_functionC(void)
{
    int a = 1;
    int c = 0;

    macro_definition_A(a, c);
}


