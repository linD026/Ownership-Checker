int func(int a, int __mut b, int __brw *c, int __brw *d)
{
    int __brw *e = d;
    int f = a + c;

    a = 1;
    b = 2;
    *c = 3;
    *d = 4;

    {
        a = 1;
    }

    *e = 1;
}

int func2(int a, int __mut b, int __brw *c, int __brw *d)
{
    a = 1;
    b = 3;

    func(a, b, c, d + d);
    b = 1;
}
