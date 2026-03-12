# MIT 6.S081 xv6 Lab Implementations

This repository contains my personal implementation of the [MIT 6.S081-2021 xv6 Labs](https://pdos.csail.mit.edu/6.S081/2021/schedule.html), a comprehensive operating system course that covers fundamental OS concepts through hands-on programming assignments.

## Overview

| Lab | Branch | Topic | Status | Detailed Report |
|-----|--------|-------|--------|-----------------|
| Lab 1 | `util` | Utilities | Completed | [lab1-util-report.md](lab1-util-report.md) |
| Lab 2 | `syscall` | System Calls | Completed | [lab2-syscall-report.md](lab2-syscall-report.md) |
| Lab 3 | `pgtbl` | Page Tables | Completed | [lab3-pgtbl-report.md](lab3-pgtbl-report.md) |
| Lab 4 | `traps` | Traps | Completed | [lab4-traps-report.md](lab4-traps-report.md) |
| Lab 5 | `cow` | Copy-on-Write | Completed | [lab5-cow-report.md](lab5-cow-report.md) |
| Lab 6 | `fs` | File System | Completed | [lab6-fs-report.md](lab6-fs-report.md) |
| Lab 7 | `thread` | Multithreading | Completed | [lab7-thread-report.md](lab7-thread-report.md) |

To run any lab, switch to the corresponding branch and run `make qemu`.

### Detailed Implementation Reports

Each lab has a comprehensive report containing:
- **Full implementation code** with syntax highlighting
- **Detailed explanations** of design decisions and algorithms
- **Architecture diagrams** showing data structures and flow
- **Testing instructions** with example output
- **Key concepts learned** from each assignment

📄 **Quick Links**: [Lab 1](lab1-util-report.md) | [Lab 2](lab2-syscall-report.md) | [Lab 3](lab3-pgtbl-report.md) | [Lab 4](lab4-traps-report.md) | [Lab 5](lab5-cow-report.md) | [Lab 6](lab6-fs-report.md) | [Lab 7](lab7-thread-report.md)

---

## Lab 1: Utilities (`util` branch)

**Objective**: Familiarize with xv6 user-space programming by implementing Unix utilities.

### 1.1 Sleep
A simple program that pauses for a specified number of ticks using the `sleep()` system call.

**Key Implementation**:
- Parse command-line argument using `atoi()`
- Call `sleep(t)` to pause execution
- Proper error handling for missing arguments

### 1.2 PingPong
Demonstrates inter-process communication using pipes.

**Key Implementation**:
- Create two pipes for bidirectional communication
- Parent sends "ping" to child through pipe p1
- Child receives and prints message, then sends "pong" back through pipe p2
- Uses `fork()`, `pipe()`, `read()`, `write()` system calls

### 1.3 Primes
Implements the Sieve of Eratosthenes using a pipeline of processes.

**Key Implementation**:
- Recursive pipeline structure where each process filters multiples of one prime
- Numbers 2-35 flow through the pipeline
- Each process prints the first number it receives (a prime), then filters out its multiples
- Demonstrates elegant use of process forking and pipe chaining

### 1.4 Find
A simplified version of the Unix `find` command that recursively searches for files by name.

**Key Implementation**:
- Recursive directory traversal using `opendir()` and `readdir()`
- Skip `.` and `..` entries to avoid infinite loops
- Use `stat()` to determine file types
- Match filenames using `strcmp()`

---

## Lab 2: System Calls (`syscall` branch)

**Objective**: Learn to add new system calls to the xv6 kernel.

### 2.1 Trace
Implements a system call tracing feature that logs when specified system calls are invoked.

**Key Implementation**:
- Added `sys_trace()` system call that takes a bitmask
- Modified `syscall()` to check and print trace information
- Each system call number has a corresponding bit in the mask
- Output format: `pid: syscall_name -> return_value`

### 2.2 Sysinfo
Implements a system call that returns system information.

**Key Implementation**:
- Added `sys_sysinfo()` to gather:
  - **Free memory**: Count pages in kernel's free list
  - **Number of processes**: Count processes with state != UNUSED
- Used `copyout()` to safely transfer data to user space
- Defined `struct sysinfo` in kernel header

---

## Lab 3: Page Tables (`pgtbl` branch)

**Objective**: Understand virtual memory and page table manipulation.

### 3.1 Speed Up System Calls
Optimized system call latency by mapping a read-only page at a fixed user-space address.

**Key Implementation**:
- Map a page (USYSCALL) shared between kernel and user space
- Store process ID in this page during context switch
- User programs can read `getpid()` result directly without trapping into kernel
- Significantly reduces overhead for frequently-called system calls

### 3.2 Print Page Table
Implemented a function to dump the contents of a page table for debugging.

**Key Implementation**:
- Recursively traverse the 3-level RISC-V page table structure
- Print valid entries with virtual to physical address mapping
- Display permission bits (R/W/X/U)
- Format: `.. .. .. page va -> pa perm`

### 3.3 Page Access (pgaccess)
Implemented a system call to detect which pages have been accessed.

**Key Implementation**:
- `sys_pgaccess()` takes: start address, number of pages, user buffer
- Use RISC-V Accessed (A) bit in PTE to track page access
- Walk page table using `walk()` to get PTEs
- Check and clear A bit, store results in bitmask
- Supports up to 32 pages at once

---

## Lab 4: Traps (`traps` branch)

**Objective**: Understand RISC-V trap handling and implement kernel features using traps.

### 4.1 Backtrace
Implemented a function to print the call stack for debugging.

**Key Implementation**:
- Walk up the stack using frame pointer (s0 register)
- Each stack frame contains saved return address and previous frame pointer
- Continue until reaching the top of the stack (page boundary)
- Print return addresses using `%p` format

### 4.2 Alarm
Implemented a facility for periodic user-level interrupts.

**Key Implementation**:
- Added two new system calls:
  - `sigalarm(interval, handler)`: Set periodic alarm
  - `sigreturn()`: Return from alarm handler
- Modified `usertrap()` to check alarm ticks on timer interrupts
- Save user trapframe before calling handler (to preserve state)
- Prevent reentrant alarm handlers with `inhandler` flag
- Restore original execution context via `sigreturn()`

**Key Code Flow**:
```c
if(p->alarminterval > 0 && p->inhandler == 0){
    p->alarmticks++;
    if(p->alarmticks == p->alarminterval){
        p->alarmticks = 0;
        memmove(p->interptf, p->trapframe, sizeof(struct trapframe));
        p->inhandler = 1;
        p->trapframe->epc = p->alarmhandler;  // Jump to handler
    }
}
```

---

## Lab 5: Copy-on-Write (`cow` branch)

**Objective**: Implement copy-on-write fork to optimize memory usage.

### Key Implementation Details

**COW Page Table Entries**:
- Added custom `PTE_COW` flag to mark copy-on-write pages
- When forking, share physical pages between parent and child
- Clear `PTE_W` and set `PTE_COW` for writable pages

**Reference Counting**:
- Implemented reference counting for physical pages
- Track how many processes share each physical page
- Only free pages when reference count reaches zero

**Modified Functions**:
- `uvmcopy()`: Instead of copying pages, increment reference count and share
- `usertrap()`: Handle page faults on COW pages
- `kalloc()`: Initialize reference count to 1
- `kfree()`: Decrement reference count, free only when 0

**Page Fault Handler**:
```c
if(cow_page_fault){
    // Allocate new page
    // Copy content from shared page
    // Update PTE with new physical address and PTE_W
    // Decrement old page's reference count
}
```

**Benefits**:
- `fork()` is much faster (no memory copy)
- Saves physical memory when processes share read-only data
- Pages are only copied when actually modified

---

## Lab 6: File System (`fs` branch)

**Objective**: Extend xv6 file system to support larger files.

### Large File Support

Extended the file size limit by implementing doubly-indirect block addressing.

**Original Structure**:
- 12 direct blocks (NDIRECT)
- 1 singly-indirect block (NINDIRECT)
- Maximum file size: 256 KB (268 blocks × 1KB)

**New Structure**:
- 11 direct blocks (reduced to make room)
- 1 singly-indirect block
- 1 doubly-indirect block
- Maximum file size: ~1 GB (much larger!)

**Modified Functions**:

1. **bmap()**: Extended to handle doubly-indirect blocks
   - Direct blocks: `bn < NDIRECT`
   - Singly-indirect: `bn < NDIRECT + N_SINGLE_INDIRECT`
   - Doubly-indirect: higher block numbers

2. **itrunc()**: Extended to free doubly-indirect blocks
   - Free direct blocks
   - Free singly-indirect blocks
   - Free doubly-indirect blocks (traverse two levels)

**inode Structure Changes**:
```c
// Original
uint addrs[NDIRECT+1];  // 12 direct + 1 indirect

// New
uint addrs[NDIRECT+1+1];  // 11 direct + 1 singly-indirect + 1 doubly-indirect
```

---

## Lab 7: Multithreading (`thread` branch)

**Objective**: Implement user-level threading by creating a thread library.

### Uthread Implementation

Created a cooperative user-level threading library with context switching.

**Thread Structure**:
```c
struct thread {
  int id;
  char stack[STACK_SIZE];  // 8KB stack per thread
  int state;                // FREE, RUNNING, RUNNABLE
  struct context ctx;       // Saved registers
};
```

**Context Structure** (callee-saved registers):
```c
struct context {
  uint64 ra;    // Return address
  uint64 sp;    // Stack pointer
  uint64 s0-s11; // Saved registers
};
```

**Key Functions**:

1. **thread_init()**: Initialize thread table, set up main thread

2. **thread_create(func)**: 
   - Find a FREE thread slot
   - Set state to RUNNABLE
   - Initialize context:
     - `ra` = function address
     - `sp` and `s0` = top of thread's stack

3. **thread_schedule()**: 
   - Round-robin scheduling of RUNNABLE threads
   - Save current context, restore next thread's context
   - Call assembly `thread_switch()` for actual context switch

4. **thread_yield()**: 
   - Voluntarily give up CPU
   - Set state to RUNNABLE and call scheduler

**Assembly Context Switch** (`thread_switch.S`):
- Save callee-saved registers (ra, sp, s0-s11) of old thread
- Restore callee-saved registers of new thread
- Return to new thread's execution

**Synchronization**:
- Cooperative multitasking (threads yield voluntarily)
- Threads synchronize using shared variables and `thread_yield()`

---

## Running the Labs

```bash
# Clone and navigate to repo
cd MIT-6.S081-2021-xv6-lab

# Switch to desired lab branch
git checkout util      # Lab 1
git checkout syscall   # Lab 2
git checkout pgtbl     # Lab 3
git checkout traps     # Lab 4
git checkout cow       # Lab 5
git checkout fs        # Lab 6
git checkout thread    # Lab 7

# Build and run
make clean
make qemu

# Run tests
make grade
```

---

## Key Learnings

1. **System Calls**: Understanding the transition from user mode to kernel mode, parameter passing, and security considerations.

2. **Virtual Memory**: Deep understanding of page tables, address translation, and memory protection mechanisms.

3. **Trap Handling**: How the kernel handles exceptions, interrupts, and system calls; saving and restoring execution context.

4. **Memory Management**: Copy-on-write optimization, reference counting, and efficient memory sharing.

5. **File Systems**: On-disk data structures, block allocation, and extending file system capabilities.

6. **Concurrency**: User-level threading, context switching, and cooperative multitasking.

---

## Resources

- [MIT 6.S081 Course Website](https://pdos.csail.mit.edu/6.S081/2021/schedule.html)
- [xv6 Book](https://pdos.csail.mit.edu/6.S081/2021/xv6/book-riscv-rev2.pdf)
- [RISC-V ISA Specification](https://riscv.org/technical/specifications/)

---

*This repository represents a complete implementation of all xv6 labs from MIT 6.S081 (Fall 2021).*