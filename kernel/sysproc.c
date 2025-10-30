#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

extern struct proc proc[NPROC];

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  if (argaddr(0, &p) < 0) return -1;
  return wait(p);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

// Lab3: return per-process state times by pid
uint64 sys_pstate(void) {
  int pid;
  uint64 u_running, u_runnable, u_sleep;
  if (argint(0, &pid) < 0) return -1;
  if (argaddr(1, &u_running) < 0) return -1;
  if (argaddr(2, &u_runnable) < 0) return -1;
  if (argaddr(3, &u_sleep) < 0) return -1;

  // Take a time snapshot BEFORE acquiring any p->lock to avoid lock order inversion.
  uint now;
  acquire(&tickslock);
  now = ticks;
  release(&tickslock);

  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      uint rtime = p->running_time;
      uint rutime = p->runnable_time;
      uint sltime = p->sleep_time;
      // include time in current state so far; guard against now < state_start_tick
      uint sst = p->state_start_tick;
      uint delta = (now >= sst) ? (now - sst) : 0;
      enum procstate st = p->state;
      switch (st) {
        case RUNNING:
          rtime += delta;
          break;
        case RUNNABLE:
          rutime += delta;
          break;
        case SLEEPING:
          sltime += delta;
          break;
        default:
          break;
      }
      int ok = 0;
      ok |= copyout(myproc()->pagetable, u_running, (char *)&rtime, sizeof(uint));
      ok |= copyout(myproc()->pagetable, u_runnable, (char *)&rutime, sizeof(uint));
      ok |= copyout(myproc()->pagetable, u_sleep, (char *)&sltime, sizeof(uint));
      release(&p->lock);
      return ok < 0 ? -1 : 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Lab3: return per-CPU user-mode running time
uint64 sys_cpustate(void) {
  uint64 u_base;
  if (argaddr(0, &u_base) < 0) return -1;

  for (int i = 0; i < NCPU; i++) {
    uint t = cpus[i].user_time;
    if (copyout(myproc()->pagetable, u_base + i * sizeof(uint), (char *)&t, sizeof(uint)) < 0)
      return -1;
  }
  return 0;
}
