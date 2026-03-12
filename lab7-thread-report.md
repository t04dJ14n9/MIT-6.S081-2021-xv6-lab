# Lab 7: Multithreading - Implementation Report

## Overview
This lab implements a user-level threading library with cooperative multitasking. The implementation includes context switching, thread scheduling, and synchronization primitives entirely in user space.

---

## Key Concepts

**User-Level Threads**:
- Threads managed entirely in user space without kernel involvement
- Fast context switches (no kernel trap needed)
- Cooperative scheduling (threads yield voluntarily)

**Context Switching**:
- Save callee-saved registers (s0-s11, sp, ra) of current thread
- Restore callee-saved registers of next thread
- Resume execution at saved return address

**Cooperative Multitasking**:
- Threads explicitly yield control via `thread_yield()`
- No preemption by timer interrupts
- Simpler but requires well-behaved threads

---

## Implementation

### File: `user/uthread.c`

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Possible states of a thread: */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

int debug = 0;

struct thread {
  int id;
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct context     ctx;          /* swtch() here to run thread */
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(uint64, uint64);

void
print_all_thread(void)
{
  for(int i = 0; i < MAX_THREAD; i++){
    printf("thread[%d]: id=%d, state=%d\n", i, all_thread[i].id, all_thread[i].state);
  }
}

void
thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule().  it needs a stack so that the first thread_switch() can
  // save thread 0's state.  thread_schedule() won't run the main thread ever
  // again, because its state is set to RUNNING, and thread_schedule() selects
  // a RUNNABLE thread.
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
  current_thread->id = 0;
  memset(&current_thread->ctx, 0, sizeof(current_thread->ctx));

  int id = 1;
  for (struct thread *t = all_thread + 1; t < all_thread + MAX_THREAD; t++) {
    t->state = FREE;
    t->id = id;
    memset(&t->ctx, 0, sizeof(t->ctx));
    id ++;
  }
}

void
thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* Find another runnable thread. */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if (debug)
      printf("i = %d, t.state = %d\n", i, t->state);
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;

    if(debug){
      print_all_thread();
      printf("thread_switch: %d -> %d\n", (t - all_thread), (next_thread - all_thread));
    }

    thread_switch((uint64)&(t->ctx), (uint64)&(next_thread->ctx));
  } else
    next_thread = 0;
}

void
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  
  // Initialize thread context
  t->ctx.ra = (uint64)func;     // Return address = function to run
  t->ctx.s0 = (uint64)t->stack + STACK_SIZE;  // Frame pointer
  t->ctx.sp = (uint64)t->stack + STACK_SIZE;  // Stack pointer
}

void
thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

// Test thread functions
volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void
thread_a(void)
{
  int i;
  printf("thread_a started\n");
  a_started = 1;
  while(b_started == 0 || c_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  current_thread->state = FREE;
  thread_schedule();
}

void
thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while(a_started == 0 || c_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void
thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while(a_started == 0 || b_started == 0)
    thread_yield();

  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int
main(int argc, char *argv[])
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}
```

### File: `user/uthread_switch.S`

```asm
        .text

        /*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

        .globl thread_switch
thread_switch:
        /* Save old thread's registers */
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        /* Load new thread's registers */
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

        ret
```

---

## Explanation

### Thread Control Block (TCB)

```c
struct thread {
  int id;                      // Thread identifier
  char stack[STACK_SIZE];      // 8KB private stack
  int state;                   // FREE, RUNNING, or RUNNABLE
  struct context ctx;          // Saved register state
};
```

### Context Structure

```c
struct context {
  uint64 ra;    // Return address - where to resume execution
  uint64 sp;    // Stack pointer
  uint64 s0;    // Frame pointer
  uint64 s1-11; // Callee-saved registers
};
```

**Why Callee-Saved Registers?**
- RISC-V calling convention divides registers into caller-saved and callee-saved
- Callee-saved (s0-s11): Must be preserved across function calls
- Caller-saved: Can be clobbered, so caller saves if needed
- For context switch, we only need callee-saved registers

### Thread Initialization

```
Initial State:
+------------------+
| Thread 0         |  <-- current_thread (main thread)
| State: RUNNING   |
| Stack: (unused)  |
+------------------+
| Thread 1         |
| State: FREE      |
+------------------+
| Thread 2         |
| State: FREE      |
+------------------+
| Thread 3         |
| State: FREE      |
+------------------+
```

### Thread Creation

```
thread_create(func):
1. Find FREE slot in all_thread[]
2. Set state to RUNNABLE
3. Initialize context:
   - ra = func (will "return" to this function)
   - sp = &stack[STACK_SIZE] (top of stack, grows down)
   - s0 = sp (frame pointer)
```

### Context Switch

```
thread_switch(&old_ctx, &new_ctx):

Save Phase:
  Store ra  -> old_ctx[0]
  Store sp  -> old_ctx[8]
  Store s0  -> old_ctx[16]
  ...
  Store s11 -> old_ctx[104]

Restore Phase:
  Load new_ctx[0]  -> ra
  Load new_ctx[8]  -> sp
  Load new_ctx[16] -> s0
  ...
  Load new_ctx[104] -> s11

Return to new thread's ra
```

### Scheduling Algorithm

```
thread_schedule():
1. Start search from current_thread + 1
2. Scan all_thread[] circularly for RUNNABLE thread
3. If found:
   a. Set next_thread->state = RUNNING
   b. current_thread = next_thread
   c. thread_switch(old_ctx, new_ctx)
4. If not found: panic (no runnable threads)
```

### Thread State Transitions

```
       thread_create()
FREE ------------> RUNNABLE
                    |    ^
          schedule()|    | yield()
                    v    |
                  RUNNING
                    |
         exit/free  |
                    v
                   FREE
```

---

## Thread Synchronization

### Barrier Synchronization Example

```c
// Global variables
volatile int a_started = 0;
volatile int b_started = 0;
volatile int c_started = 0;

void thread_a(void) {
  printf("thread_a started\n");
  a_started = 1;  // Signal that A has started
  
  // Wait for B and C
  while(b_started == 0 || c_started == 0)
    thread_yield();  // Yield while waiting
  
  // All threads ready, continue work
  for (int i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    thread_yield();
  }
  
  current_thread->state = FREE;
  thread_schedule();
}
```

**How It Works**:
1. Each thread sets its flag after starting
2. Threads spin-yield until all flags are set
3. `thread_yield()` allows other threads to run
4. Cooperative approach - threads voluntarily give up CPU

---

## Stack Layout

```
Thread Stack (grows downward):
High Address
+------------------+  <- stack + STACK_SIZE (initial sp)
|                  |
|   Stack Frame 1  |  <- Call to thread function
|                  |
+------------------+
|                  |
|   Stack Frame 2  |  <- Nested calls
|                  |
+------------------+
|       ...        |
+------------------+
|   Stack Frame N  |
+------------------+  <- stack (bottom)
Low Address
```

**Stack Size**: 8192 bytes (8 KB) per thread

---

## Testing

```bash
$ uthread
thread_a started
thread_b started
thread_c started
thread_a 0
thread_b 0
thread_c 0
thread_a 1
thread_b 1
thread_c 1
...
thread_a 99
thread_b 99
thread_c 99
thread_a: exit after 100
thread_b: exit after 100
thread_c: exit after 100
$
```

**Expected Behavior**:
- All three threads print numbers 0-99
- Output is interleaved due to cooperative scheduling
- Each thread yields after printing a number
- Final counts should all be 100

---

## Comparison with Kernel Threads

| Aspect | User Threads (uthread) | Kernel Threads |
|--------|------------------------|----------------|
| Context Switch | User space (fast) | Kernel space (slower) |
| Creation | Fast | Slower |
| Scheduling | Cooperative | Preemptive |
| Blocking | Blocks all threads | Blocks only one thread |
| Parallelism | Single CPU | Multiple CPUs |

---

## Summary

### Key Functions

| Function | Purpose |
|----------|---------|
| `thread_init()` | Initialize thread table and main thread |
| `thread_create()` | Create a new thread with given function |
| `thread_schedule()` | Find and switch to next runnable thread |
| `thread_yield()` | Voluntarily give up CPU |
| `thread_switch()` | Assembly context switch routine |

### Key Data Structures

| Structure | Purpose |
|-----------|---------|
| `struct thread` | Thread control block (TCB) |
| `struct context` | Saved register state |
| `all_thread[]` | Global thread table |
| `current_thread` | Pointer to currently running thread |

### Key Concepts Learned
1. **User-level threading**: Implementing threads without kernel support
2. **Context switching**: Saving/restoring callee-saved registers
3. **Calling conventions**: Understanding caller-saved vs callee-saved
4. **Cooperative scheduling**: Threads yield voluntarily
5. **Stack management**: Each thread has private stack
6. **Synchronization**: Using shared variables and yielding
7. **Assembly programming**: Writing context switch in RISC-V assembly
