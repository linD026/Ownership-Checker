struct data_struct {
    int *ptrA;
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
    // TODO: init

    data = obj;
    // data = &scope_data;

    // funcB(data->ptrA);

    return data;
}
