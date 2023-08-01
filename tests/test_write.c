int function2(int a, int b, int c)
{
    return 0;
}

int function(int __mut mutable, int __brw borrow)
{
    int a = 0;

    a = borrow = 3;

    function2(a, mutable, borrow);
}
