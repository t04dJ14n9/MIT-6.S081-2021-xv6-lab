#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  // parse argument
  uint64 buf;
  if (argaddr(0, &buf) < 0) {
    return -1;
  }

  int size;
  if (argint(1, &size) < 0) {
    return -1;
  }

  uint64 dstva;
  if (argaddr(2, &dstva) < 0) {
    return -1;
  }

  if (size > 32) {
    printf("pgaccess cannot handle size > 32\n");
  }

  // kernel buffer for storing access bits
  unsigned int abits = 0;

  struct proc *p = myproc();
  uint64 base = PGROUNDDOWN(buf);
  for (int i = 0; i < size; i ++){
    uint64 va = base + PGSIZE * i;
    pte_t *pte = walk(p->pagetable, va, 0);
    if (*pte & PTE_A){
      abits |= (1L << i);
      *pte -= PTE_A;
    }
  }
  if (copyout(p->pagetable, dstva, (char *)&abits, 4) < 0) {
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
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
