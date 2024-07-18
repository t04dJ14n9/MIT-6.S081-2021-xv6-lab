#include "kernel/types.h"
#include "user/user.h"

int main()
{
    char *s = "1a2";
    printf("%d", atoi(s));
    exit(0);
}