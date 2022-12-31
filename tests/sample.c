int *funcA(int __mut __brw *var)
{
    int *local = malloc();
    return local;
}

// change ownership
int funcB(int *var)
{
    // check free
    free(var);
}

int main(void)
{
    int __mut *varA =  malloc();

    // check mut and brw type
    int *varB = funcA(borrow(var));

    funcB(varB);
    // varB cannot use after funcB()

    // check free
    free(varA);

    return 0;
}
