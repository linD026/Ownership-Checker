struct data_struct {
    int __mut *ptrA;
    int varA;
    unsigned long *ptrB;
};

struct recursive_struct {
    struct nested_struct {
        unsigned long ptrA;
    } rs_A;
    unsigned long ptrB;
    struct {
        unsigned long ptrC;
        struct nested_struct_A {
            unsigned long ptrD;
        } rs_B;
    } anon_A;
};

void pass_struct_member_function(int *ptr)
{
}

struct data_struct *function(struct data_struct *obj)
{
    struct data_struct *data;
    struct scope_data_struct {
        int *ptrA;
        int varA;
        unsigned long *ptrB;
    };
    // } scope_data;
    struct scope_data_struct scope_data;
    int a;
    // TODO: init

    data = obj;
    // data = &scope_data;

    pass_struct_member_function(data+1);
    data->ptrA = &a;
    pass_struct_member_function(data->ptrA);

    return data;
}
