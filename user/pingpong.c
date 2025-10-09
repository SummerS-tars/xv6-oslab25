#include "user/user.h"
#include "kernel/types.h"

int main(int argc, char *argv[])
{
    int f2c[2], c2f[2];
    if(pipe(f2c) < 0 || pipe(c2f) < 0)
    {
        fprintf(2, "pipe error\n");
        exit(1);
    }

    uint64 fpid = getpid();
    uint64 pid = fork();
    if(pid < 0)
    {
        fprintf(2, "fork error\n");
        exit(1);
    }

    if(pid > 0)
    { // father
        char buf[10] = {0};
        // close the f2c read end and c2f write end
        close(f2c[0]);
        close(c2f[1]);
        write(f2c[1], "ping", 4);
        read(c2f[0], buf, 10);
        fprintf(1, "%d: received %s from pid %d\n", fpid, buf, pid);
        close(f2c[1]);
        close(c2f[0]);
    }
    else 
    { // child
        char buf[10] = {0};
        // close the f2c write end and c2f read end
        close(f2c[1]);
        close(c2f[0]);
        read(f2c[0], buf, 10);
        fprintf(1, "%d: received %s from pid %d\n", getpid(), buf, fpid);
        write(c2f[1], "pong", 4);
        close(f2c[0]);
        close(c2f[1]);
    }

    exit(0);
}