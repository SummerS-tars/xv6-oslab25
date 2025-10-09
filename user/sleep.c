#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        fprintf(2, "sleep: missing operand\n");
        exit(1);
    }

    int sleep_time;
    sleep_time = atoi(argv[1]);
    if(sleep_time < 0)
    {
        fprintf(2, "sleep: invalid time\n");
        exit(1);
    }

    sleep(sleep_time);
    fprintf(1, "(nothing happens for a little while)\n");
    exit(0);
}