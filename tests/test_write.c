int *function2(int a, int __mut *b, int *c)
{
    function2(a, b, c);
    return (b + 1);
}

int *function(int __mut *mutable, int __brw *borrow)
{
    int a = 0;

    a = borrow = 3;

    function2(a, mutable, borrow);

    return mutable;
}
