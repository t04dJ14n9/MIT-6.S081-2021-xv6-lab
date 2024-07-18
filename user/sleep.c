#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: Sleep t, where t is the time to sleep in seconds.\n");
        exit(-1);
    }
    int t = atoi(argv[1]);
    sleep(t);
    exit(0);
}