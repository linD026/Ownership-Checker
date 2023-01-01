int func(int a, int __mut b, int __brw *c, int __mut __brw *d)
{
    a = 1;
    b = 2;
    *c = 3;
    *d = 4;

    {
        a = 1;
    }
}

int func2(int a, int __mut b, int __brw *c, int __mut __brw *d)
{
    a = 1;
    b = 3;
}
