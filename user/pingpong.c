#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int p1[2];
    pipe(p1); // parent to child
    int p2[2];
    pipe(p2);        // child to parent
    if (fork() == 0) // child
    {
        char bufRead[100];
        if (read(p1[0], bufRead, 100) > 0)
        {
            if (strcmp(bufRead, "114514") == 0)
            {
                fprintf(1, "%d: received ping\n", getpid());
            }
            else
            {
                fprintf(1, "%d: received wrong ping message\n", getpid());
            }
        }
        else
        {
            exit(-1);
        }
        char *bufWrite = "114514";
        if (write(p2[1], bufWrite, strlen(bufWrite)) == strlen(bufWrite))
        {
            exit(0);
        }
    }
    else // parent
    {
        char *bufWrite = "114514";
        if (write(p1[1], bufWrite, strlen(bufWrite)) == strlen(bufWrite))
        {
            char bufRead[100];
            if (read(p2[0], bufRead, 100) > 0)
            {
                if (strcmp(bufRead, "114514") == 0)
                {
                    fprintf(1, "%d: received pong\n", getpid());
                }
                else
                {
                    fprintf(1, "%d: received wrong ping message\n", getpid());
                }
            }
            else
            {
                exit(-1);
            }
        }
    }
    exit(0);
}