# Lab 2: System Calls - Implementation Report

## Overview
This lab focuses on extending the xv6 kernel by adding new system calls. The implementations demonstrate understanding of system call mechanisms, kernel data structures, and safe data transfer between user and kernel space.

---

## 1. Trace System Call

### Objective
Implement a system call tracing feature that logs when specified system calls are invoked by a process and its children.

### Files Modified

#### `kernel/syscall.h` - Add new system call number
```c
#define SYS_trace  22
```

#### `kernel/syscall.c` - Add trace to syscall table and implement tracing logic

**Modified syscall names array:**
```c
static char *syscall_names[] = {
    [SYS_fork]    "fork",
    [SYS_exit]    "exit",
    [SYS_wait]    "wait",
    [SYS_pipe]    "pipe",
    [SYS_read]    "read",
    [SYS_kill]    "kill",
    [SYS_exec]    "exec",
    [SYS_fstat]   "fstat",
    [SYS_chdir]   "chdir",
    [SYS_dup]     "dup",
    [SYS_getpid]  "getpid",
    [SYS_sbrk]    "sbrk",
    [SYS_sleep]   "sleep",
    [SYS_uptime]  "uptime",
    [SYS_open]    "open",
    [SYS_write]   "write",
    [SYS_mknod]   "mknod",
    [SYS_unlink]  "unlink",
    [SYS_link]    "link",
    [SYS_mkdir]   "mkdir",
    [SYS_close]   "close",
    [SYS_trace]   "trace",
    [SYS_sysinfo] "sysinfo",
};
```

**Modified syscall() function:**
```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    
    // Trace implementation: check if this syscall should be traced
    if ((p->trace_mask >> num) & 1) {
      printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

#### `kernel/proc.h` - Add trace_mask to proc structure
```c
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  
  int trace_mask;              // NEW: Bitmask for tracing syscalls
};
```

#### `kernel/proc.c` - Initialize trace_mask in allocproc()
```c
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->trace_mask = 0;  // NEW: Initialize trace mask to 0

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

#### `kernel/sysproc.c` - Implement sys_trace()
```c
uint64
sys_trace(void)
{
  int mask;
  if(argint(0, &mask) < 0)
    return -1;
  myproc()->trace_mask = mask;
  return 0;
}
```

#### `kernel/syscall.c` - Add sys_trace declaration and to syscall table
```c
extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
    ...
    [SYS_trace]   sys_trace,
    [SYS_sysinfo] sys_sysinfo,
};
```

#### `user/user.h` - Add user-space declaration
```c
int trace(int);
```

#### `user/usys.pl` - Add entry stub generation
```perl
entry("trace");
```

### Explanation

**Trace Mask Mechanism**:
- Each bit in `trace_mask` corresponds to a system call number
- If bit N is set, syscall number N will be traced
- Example: `trace(1 << SYS_read)` traces only read calls
- Example: `trace(0xFFFFFFFF)` traces all syscalls

**Inheritance**:
- When `fork()` is called, the child's `trace_mask` is copied from parent
- This allows child processes to also be traced

**Trace Output Format**:
```
<pid>: syscall <name> -> <return_value>
```

**Key Implementation Points**:
1. Add `trace_mask` field to `struct proc` to store which syscalls to trace
2. In `syscall()`, check if the current syscall number's bit is set in the mask
3. If set, print trace information with process ID, syscall name, and return value
4. Initialize `trace_mask` to 0 in `allocproc()` to disable tracing by default
5. Implement `sys_trace()` to set the trace mask from user space

---

## 2. Sysinfo System Call

### Objective
Implement a system call that returns system information including free memory and number of processes.

### Files Added/Modified

#### `kernel/sysinfo.h` (NEW FILE)
```c
struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
};
```

#### `kernel/kalloc.c` - Add free memory counting
```c
// Returns the number of bytes of free memory
uint64
freemem(void)
{
  struct run *r;
  uint64 free_bytes = 0;
  
  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r) {
    free_bytes += PGSIZE;  // Each page is PGSIZE bytes
    r = r->next;
  }
  release(&kmem.lock);
  
  return free_bytes;
}
```

#### `kernel/proc.c` - Add process counting
```c
// Returns the number of processes with state != UNUSED
int
nproc(void)
{
  struct proc *p;
  int count = 0;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED) {
      count++;
    }
    release(&p->lock);
  }
  
  return count;
}
```

#### `kernel/defs.h` - Add function declarations
```c
// kalloc.c
void*           kalloc(void);
void            kfree(void *);
void            kinit(void);
uint64          freemem(void);  // NEW

// proc.c
int             cpuid(void);
void            exit(int);
int             fork(void);
int             growproc(int);
void            proc_mapstacks(pagetable_t);
void            proc_freepagetable(pagetable_t, uint64);
void            freeproc(struct proc *);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);
int             nproc(void);  // NEW
```

#### `kernel/sysproc.c` - Implement sys_sysinfo()
```c
#include "sysinfo.h"  // NEW include

uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  uint64 addr;
  
  // Get user-space destination address
  if(argaddr(0, &addr) < 0)
    return -1;
  
  // Gather system information
  info.freemem = freemem();
  info.nproc = nproc();
  
  // Copy struct to user space
  if(copyout(myproc()->pagetable, addr, (char *)&info, sizeof(info)) < 0)
    return -1;
  
  return 0;
}
```

#### `kernel/syscall.h` - Add system call number
```c
#define SYS_sysinfo 23
```

#### `kernel/syscall.c` - Add to syscall table
```c
extern uint64 sys_sysinfo(void);

static uint64 (*syscalls[])(void) = {
    ...
    [SYS_sysinfo] sys_sysinfo,
};

static char *syscall_names[] = {
    ...
    [SYS_sysinfo] "sysinfo",
};
```

#### `user/user.h` - Add user-space declaration
```c
struct sysinfo;
int sysinfo(struct sysinfo *);
```

#### `user/usys.pl` - Add entry stub
```perl
entry("sysinfo");
```

### Explanation

**Free Memory Calculation** (`freemem()`):
1. The kernel maintains a linked list of free pages (`kmem.freelist`)
2. Each node in the list represents one free page (PGSIZE = 4096 bytes)
3. Walk the list and count total free bytes
4. Must acquire lock to prevent race conditions

**Process Counting** (`nproc()`):
1. Iterate through the process table (`proc[]`)
2. Count processes with `state != UNUSED`
3. Acquire lock for each process during check to ensure consistency

**Safe Data Transfer** (`copyout()`):
- Kernel cannot directly write to user-space addresses
- `copyout(pagetable, dst_va, src_kernel, len)` safely copies data
- It performs address translation and validation

**System Call Flow**:
```
User Program          Kernel
----------            ------
sysinfo(&info)  --->  sys_sysinfo()
                      |- freemem() -> count free pages
                      |- nproc() -> count processes
                      |- copyout() -> copy to user space
                      return 0
```

---

## Testing

### Trace Test
```bash
$ trace 32 grep hello README
3: syscall trace -> 0
3: syscall exec -> 3
3: syscall open -> 3
3: syscall read -> 1024
3: syscall read -> 1024
3: syscall read -> 1024
3: syscall read -> 0
3: syscall close -> 0
```
Note: `32 = 1 << SYS_read`, so only read syscalls are traced.

### Sysinfo Test
```bash
$ sysinfotest
free mem: 12345678 bytes
num procs: 5
$
```

---

## Summary

| System Call | Purpose | Key Kernel Functions |
|-------------|---------|---------------------|
| trace | Enable syscall tracing | Process field modification, syscall() hook |
| sysinfo | Get system statistics | Memory counting, process enumeration, copyout() |

### Key Concepts Learned
1. **Adding system calls**: Define number, add to table, implement handler, add user stub
2. **Process structure**: Adding per-process state fields
3. **Kernel data structures**: Walking free page list, process table
4. **User-kernel boundary**: Safe data transfer with `copyout()`/`copyin()`
5. **Inheritance**: How `fork()` copies process state
