#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

uint64 MAGIC_NUM = 10;
uint MAX_TIME = 9999;
uint64 pids[64];
uint64 results[1024];

uint64 big_calculation() {
    uint64 i, j, sum = 0;
    for (i = 0; i < MAGIC_NUM; i++) {
        for (j = 0; j < i; j++) {
            if (j & 1) {
                sum -= j;
            } else {
                sum += j;
            }
        }
        sum *= i;
        sum /= (i - j + 1);
    }
    return sum;
}

// static void write_line(const char *s) {
//     write(1, s, strlen(s));
// }

static void print_pid_line(int pid, uint running_time, uint runnable_time, uint sleep_time) {
    char buf[128];
    char num[32];
    int off = 0;

    // PID: %d, Running Time: %d, Runnable Time: %d, Sleep Time: %d\n
    const char *pfx1 = "PID: ";
    const char *pfx2 = ", Running Time: ";
    const char *pfx3 = ", Runnable Time: ";
    const char *pfx4 = ", Sleep Time: ";

    // PID
    memcpy(buf + off, pfx1, strlen(pfx1)); off += strlen(pfx1);
    itoa(pid, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
    // Running Time
    memcpy(buf + off, pfx2, strlen(pfx2)); off += strlen(pfx2);
    itoa((int)running_time, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
    // Runnable Time
    memcpy(buf + off, pfx3, strlen(pfx3)); off += strlen(pfx3);
    itoa((int)runnable_time, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
    // Sleep Time
    memcpy(buf + off, pfx4, strlen(pfx4)); off += strlen(pfx4);
    itoa((int)sleep_time, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
    // Newline
    buf[off++] = '\n';

    write(1, buf, off);
}

void loop() {
    int pid = getpid();
    uint last_running_time = 0, last_runnable_time = 0, last_sleep_time = 0;
    uint running_time, runnable_time, sleep_time;
    while (1) {
        results[pid] = big_calculation(); // 做一些计算以消耗CPU时间
        if (pstate(pid, &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", pid);
            exit(1);
        }
        
        // 时间不能倒流
        if (running_time < last_running_time || runnable_time < last_runnable_time || sleep_time < last_sleep_time) {
            printf("Error: pstate returned invalid times for PID %d\n", pid);
            exit(-1);
        }
        last_running_time = running_time;
        last_runnable_time = runnable_time;
        last_sleep_time = sleep_time;

        // 达到最大运行时间则退出
        if (running_time >= MAX_TIME) {
            print_pid_line(pid, running_time, runnable_time, sleep_time);
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: stat <n>\n");
        exit(1);
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        printf("Error: Invalid number of processes\n");
        exit(1);
    }

    int start = uptime();
    // 调用cpustate系统调用获取初始CPU时间
    uint start_cpu_time[NCPU];
    cpustate(start_cpu_time);

    for (int i = 0; i < n; i++) { // fork n个子进程以查看他们的调度运行情况
        int pid = fork();
        if (pid < 0) {
            printf("Error: Fork failed\n");
            exit(1);
        }
        pids[i] = pid;
        if (pid == 0) {
            loop();
        }
    }

    // 等待某个子进程运行完成退出
    int exitcode;
    int pid = wait(&exitcode, 0);
    if (exitcode != 0) {
        printf("Error: Child process %d exited with code %d\n", pid, exitcode);
        exit(exitcode);
    }

    // kill其他子进程
    for (int i = 0; i < n; ++i) {
        kill(pids[i]);
    }

    // 输出所有子进程的状态
    for (int i = 0; i < n; ++i) {
        if (pids[i] == pid) { // 自己退出的那个子进程已经自己输出了结果，不需要父进程帮忙输出。父进程也无法使用pstate查看他的状态，为什么？
            continue;
        }
        uint running_time, runnable_time, sleep_time;
        if (pstate(pids[i], &running_time, &runnable_time, &sleep_time) < 0) {
            printf("Error: pstate failed for PID %d\n", pids[i]);
            exit(1);
        }
        print_pid_line(pids[i], running_time, runnable_time, sleep_time);
    }

    uint end_cpu_time[NCPU];
    uint total_cpu_time = 0;
    // 获取结束时的CPU时间，与开始CPU时间作差获得该程序中CPU的运行时间
    cpustate(end_cpu_time);
    for (int i = 0; i < NCPU; ++i) {
        {
            char buf[64];
            char num[32];
            int off = 0;
            const char *p1 = "CPU ";
            const char *p2 = ": Running Time: ";
            memcpy(buf + off, p1, strlen(p1)); off += strlen(p1);
            itoa(i, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
            memcpy(buf + off, p2, strlen(p2)); off += strlen(p2);
            itoa((int)(end_cpu_time[i] - start_cpu_time[i]), num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
            buf[off++] = '\n';
            write(1, buf, off);
        }
        total_cpu_time += end_cpu_time[i] - start_cpu_time[i];
    }
    {
        char buf[64];
        char num[32];
        int off = 0;
        const char *p = "Total CPU Running Time: ";
        memcpy(buf + off, p, strlen(p)); off += strlen(p);
        itoa((int)total_cpu_time, num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
        buf[off++] = '\n';
        write(1, buf, off);
    }

    // stat.c的运行时间
    int end = uptime();
    {
        char buf[64]; char num[32]; int off = 0; const char *p = "Stat Time: ";
        memcpy(buf + off, p, strlen(p)); off += strlen(p);
        itoa((int)(end - start), num); memcpy(buf + off, num, strlen(num)); off += strlen(num);
        buf[off++] = '\n';
        write(1, buf, off);
    }

    exit(0);
}