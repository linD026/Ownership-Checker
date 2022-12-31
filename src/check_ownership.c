#include <osc/parser.h>

#define _VA_ENTRY(_type, _TYPE)                                             \
    {                                                                       \
        .type = #_type, .len = sizeof(#_type) - 1, .flag = VAR_ATTR_##_TYPE \
    }
#define VA_ENTRY(type, TYPE) _VA_ENTRY(type, TYPE)

const struct type_info var_attr_table[] = { VA_ENTRY(__brw, BRW),
                                            VA_ENTRY(__mut, MUT) };

int check_ownership(void)
{
}
