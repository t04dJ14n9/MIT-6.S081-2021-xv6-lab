#include "kernel/types.h"
#include "user/user.h"

// p is the input pipe
int sieve(int *p)
{
    int prime;
    int res;
    int po[2];
    pipe(po);
    read(p[0], &prime, sizeof(prime));
    fprintf(1, "prime %d\n", prime);
    int childCreated = 0;
    int poClosed = 0;
    while (read(p[0], &res, sizeof(res)) > 0)
    {
        if (!childCreated)
        {
            childCreated = 1;
            if (fork() == 0)
            {
                close(p[0]);
                close(po[1]);
                sieve(po);
            }
        }
        if (!poClosed)
        {
            close(po[0]);
            poClosed = 1;
        }
        if (res % prime != 0)
        {
            write(po[1], &res, sizeof(res));
        }
    }
    close(po[1]);
    wait(0);
    exit(0);
};

int main()
{
    int p[2];
    pipe(p);
    if (fork() == 0)
    {
        close(p[1]);
        sieve(p);
    }
    else
    {
        // no need to read
        close(p[0]);
        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(i));
        }
        // write finished
        close(p[1]);
        wait(0);
        exit(0);
    }
    return 0;
};