# Lab 4: Traps - Implementation Report

## Overview
This lab focuses on understanding RISC-V trap handling and implementing kernel features using traps. The implementations demonstrate exception handling, stack unwinding, and timer-based user-level interrupts.

---

## 1. Backtrace

### Objective
Implement a function to print the call stack for debugging purposes, showing the chain of function calls that led to the current point of execution.

### Files Modified

#### `kernel/riscv.h` - Read frame pointer register
```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

#### `kernel/defs.h` - Add backtrace declaration
```c
// printf.c
void            printf(char*, ...);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);
void            backtrace(void);  // NEW
```

#### `kernel/printf.c` - Implement backtrace()

```c
void
backtrace(void)
{
  uint64 fp = r_fp();  // Get current frame pointer
  uint64 top = PGROUNDUP(fp);  // Top of stack (page boundary)
  
  printf("backtrace:\n");
  
  // Walk up the stack
  while(fp < top) {
    // Return address is stored at fp - 8
    uint64 ra = *(uint64*)(fp - 8);
    printf("%p\n", ra);
    
    // Previous frame pointer is stored at fp - 16
    fp = *(uint64*)(fp - 16);
  }
}
```

#### `kernel/sysproc.c` - Add sys_backtrace() system call (optional)
```c
uint64
sys_backtrace(void)
{
  backtrace();
  return 0;
}
```

#### `kernel/panic.c` - Call backtrace in panic()
```c
void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  backtrace();  // NEW: Print call stack on panic
  panicked = 1;
  for(;;)
    ;
}
```

### Explanation

**Stack Frame Layout (RISC-V)**:
```
High Addresses
+------------------+
|   ...            |
+------------------+
|  Return Address  | <- fp (current frame pointer)
+------------------+
|  Saved FP (s0)   | <- fp - 8
+------------------+
|  Local vars      | <- fp - 16
|       ...        |
+------------------+
|  Return Address  | <- previous fp
+------------------+
|  Saved FP        |
+------------------+
Low Addresses
```

**Frame Pointer (s0) Convention**:
- `s0` register holds the frame pointer
- Points to the saved frame pointer of the previous function
- Return address is at `fp - 8`
- Previous frame pointer is at `fp - 16`

**Algorithm**:
1. Read current frame pointer using `r_fp()`
2. Calculate stack top (page boundary)
3. While frame pointer is within stack bounds:
   - Read return address from `fp - 8`
   - Print return address
   - Follow saved frame pointer to previous frame (`fp - 16`)

**Example Output**:
```
backtrace:
0x0000000080002de0
0x0000000080002f64
0x0000000080002b94
0x00000000800028d8
```

### Key Concepts
- **Stack unwinding**: Walking up the call stack using frame pointers
- **Stack frame layout**: Understanding how compilers organize stack frames
- **Callee-saved registers**: s0 (frame pointer) is preserved across calls

---

## 2. Alarm (Sigalarm/Sigreturn)

### Objective
Implement a facility for periodic user-level interrupts that allows a process to schedule a function to be called periodically based on timer interrupts.

### Files Modified

#### `kernel/proc.h` - Add alarm fields to proc structure
```c
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;
  void *chan;
  int killed;
  int xstate;
  int pid;

  // wait_lock must be held when using this:
  struct proc *parent;

  // these are private to the process
  uint64 kstack;
  uint64 sz;
  pagetable_t pagetable;
  struct trapframe *trapframe;
  struct context context;
  struct file *ofile[NOFILE];
  struct inode *cwd;
  char name[16];
  int trace_mask;
  struct usyscall *usyscall;
  
  // NEW: Alarm fields
  int alarminterval;    // Alarm interval in ticks (0 = disabled)
  int alarmticks;       // Ticks elapsed since last alarm
  uint64 alarmhandler;  // User-space handler function address
  int inhandler;        // Flag to prevent reentrant handlers
  struct trapframe interptf;  // Saved trapframe during handler
};
```

#### `kernel/sysproc.c` - Implement sigalarm and sigreturn

```c
uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  
  // Get interval argument
  if(argint(0, &interval) < 0)
    return -1;
  
  // Get handler function address
  if(argaddr(1, &handler) < 0)
    return -1;
  
  struct proc *p = myproc();
  
  // Set alarm parameters
  p->alarminterval = interval;
  p->alarmhandler = handler;
  p->alarmticks = 0;  // Reset tick counter
  
  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  
  // Restore original trapframe
  memmove(p->trapframe, &p->interptf, sizeof(struct trapframe));
  
  // Clear in-handler flag to allow next alarm
  p->inhandler = 0;
  
  return p->trapframe->a0;  // Return original a0 value
}
```

#### `kernel/trap.c` - Handle alarm in usertrap()

```c
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap()
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if(r_scause() == 8){
    // system call
    if(p->killed)
      exit(-1);

    p->trapframe->epc += 4;
    intr_on();
    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    // NEW: Handle alarm
    if(p->alarminterval > 0 && p->inhandler == 0){
      p->alarmticks++;
      if(p->alarmticks == p->alarminterval){
        p->alarmticks = 0;
        
        // Save user program state
        memmove(&p->interptf, p->trapframe, sizeof(struct trapframe));
        
        // Set flag to prevent reentrant handlers
        p->inhandler = 1;
        
        // Modify return address to jump to handler
        p->trapframe->epc = p->alarmhandler;
        
        // Return to user space (will execute handler)
        usertrapret();
      }
    }
    yield();
  }

  usertrapret();
}
```

#### `kernel/syscall.h` - Add system call numbers
```c
#define SYS_sigalarm  25
#define SYS_sigreturn 26
```

#### `kernel/syscall.c` - Add to syscall table
```c
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);

static uint64 (*syscalls[])(void) = {
    ...
    [SYS_sigalarm]  sys_sigalarm,
    [SYS_sigreturn] sys_sigreturn,
};
```

#### `user/user.h` - Add user-space declarations
```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

#### `user/usys.pl` - Add entry stubs
```perl
entry("sigalarm");
entry("sigreturn");
```

### Explanation

**System Call Interface**:
```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

**How It Works**:

1. **Setup Phase** (`sigalarm`):
   - User calls `sigalarm(interval, handler)`
   - Kernel stores interval and handler address in proc structure
   - Initializes `alarmticks` counter to 0

2. **Timer Interrupt Handling** (`usertrap`):
   - On each timer interrupt, increment `alarmticks`
   - When `alarmticks == alarminterval`:
     - Save current trapframe to `interptf`
     - Set `inhandler = 1` to prevent reentrancy
     - Change `trapframe->epc` to handler address
     - Return to user space (executes handler instead of continuing)

3. **Handler Execution**:
   - User-defined handler function runs
   - Handler must call `sigreturn()` when done

4. **Return Phase** (`sigreturn`):
   - Restore original trapframe from `interptf`
   - Clear `inhandler` flag
   - Return to original execution point

**State Preservation**:
```
Normal Execution          Alarm Handler               After sigreturn
+---------------+        +---------------+           +---------------+
| epc = main    |        | epc = handler |           | epc = main+4  |
| a0, a1, ...   |  --->  | (modified)    |   --->    | (restored)    |
| ...           |        |               |           |               |
+---------------+        +---------------+           +---------------+
   saved to                 executed                     restored from
   interptf                                              interptf
```

**Reentrancy Protection**:
- `inhandler` flag prevents alarm from firing while handler is running
- Without this, the handler could be interrupted by another alarm, causing stack overflow

**Example Usage**:
```c
void periodic_handler() {
    printf("ALARM!\n");
    sigreturn();  // Must call to restore state
}

int main() {
    sigalarm(10, periodic_handler);  // Call handler every 10 ticks
    // ... program continues ...
}
```

### Key Concepts
- **Timer interrupts**: Periodic interrupts for scheduling and alarms
- **Trapframe manipulation**: Modifying return address to redirect execution
- **State preservation**: Saving/restoring complete processor state
- **Reentrancy**: Preventing handlers from interrupting themselves

---

## Testing

### Backtrace Test
```bash
$ bttest
backtrace:
0x0000000080002de0
0x0000000080002f64
0x0000000080002b94
0x00000000800028d8
Test passed!
```

### Alarm Test
```bash
$ alarmtest
test0 start
..test0 passed
test1 start
..test1 passed
test2 start
..test2 passed
test3 start
..test3 passed
```

---

## Summary

| Exercise | Key Concepts | Modified Files |
|----------|-------------|----------------|
| Backtrace | Stack unwinding, frame pointers | printf.c, riscv.h |
| Alarm | Timer interrupts, trapframe manipulation, state preservation | trap.c, sysproc.c, proc.h |

### Key Concepts Learned
1. **Trap handling**: How kernel handles exceptions and interrupts
2. **Stack frames**: Layout and unwinding using frame pointers
3. **Trapframe**: Complete processor state saved during traps
4. **Timer interrupts**: Periodic interrupts for scheduling
5. **State preservation**: Saving and restoring execution context
6. **Reentrancy**: Handling nested/already-active handlers
