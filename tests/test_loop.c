
int main(void)
{
    for (int i = 0; i < 1; i++) {
        int a = 0;
        a += i;
    }

    int while_i = 10;
    while (while_i--) {
        if (while_i == 2)
            while_i--;
    }

    while_i = 10;
    do {
         if (while_i == 2)
            while_i--;
    } while (while_i--);

    return 0;
}
