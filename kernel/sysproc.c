#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_getsyscount(void)
{
  struct proc* p = myproc();
  int syscall_no;
  argint(0, &syscall_no);
  return p->num_syscalls[syscall_no];
}

uint64
sys_sigalarm(void)
{
  struct proc* p = myproc();
  int ticks; uint64 handler;

  argint(0, &ticks);
  argaddr(1, &handler);

  p->alarm_ticks = ticks;
  p->sigalarm_handler = handler;
  p->cur_ticks = 0;
  p->alarm_set = (p->alarm_ticks > 0);
  p->handler_running = 0;
  if (!ticks) return 0; // Unset the alarm if ticks is zero
  if (p->alarm_tf == 0) {
    // Allocate memory for alarm_tf
    if ((p->alarm_tf = (struct trapframe *)kalloc()) == 0) {
      return 1; // Allocation failed
    }
  }
  return 0;
}

uint64
sys_sigreturn(void)
{
    struct proc *p = myproc();
    // Restore the original trapframe
    memmove(p->trapframe, p->alarm_tf, sizeof(struct trapframe));
    p->handler_running = 0;
    return p->trapframe->a0; // return the handler's return value
}
