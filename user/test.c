#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int i1 = 114514;
    int i2 = 1919819;
    int p[2];
    pipe(p);
    if (write(p[1], &i1, sizeof(i1)) != sizeof(i1))
    {
        fprintf(2, "error writing p[1] first time");
    }
    if (write(p[1], &i2, sizeof(i2)) != sizeof(i2))
    {
        fprintf(2, "error writing p[1] second time");
    }
    int res1;
    int res2;
    if (read(p[0], &res1, sizeof(res1)) < 0)
    {
        fprintf(2, "error reading p[0] first time");
    }
    fprintf(1, "received: %d\n", res1);
    if (read(p[0], &res2, sizeof(res2)) < 0)
    {
        fprintf(2, "error reading p[0] second time");
    }
    fprintf(1, "received: %d\n", res2);
    exit(0);
}