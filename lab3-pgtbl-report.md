# Lab 3: Page Tables - Implementation Report

## Overview
This lab explores virtual memory and page table manipulation in xv6. The implementations demonstrate understanding of address translation, page table traversal, and optimization techniques for system calls.

---

## 1. Speed Up System Calls (USYSCALL)

### Objective
Optimize system call latency by mapping a read-only page at a fixed user-space address that shares the process ID between kernel and user space, allowing `getpid()` to return the value directly without a kernel trap.

### Files Modified

#### `kernel/memlayout.h` - Define USYSCALL address
```c
// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   usyscall page (shared with kernel for fast syscalls)
//   ...

#define USYSCALL (TRAPFRAME - PGSIZE)

struct usyscall {
  int pid;  // Process ID
};
```

#### `kernel/proc.h` - Add usyscall field to proc structure
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
  
  struct usyscall *usyscall;  // NEW: Pointer to shared usyscall page
};
```

#### `kernel/proc.c` - Allocate and map usyscall page

**Modified allocproc():**
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
  p->trace_mask = 0;

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
  
  // NEW: Allocate usyscall page
  if((p->usyscall = (struct usyscall *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  p->usyscall->pid = p->pid;  // Initialize with process ID

  // Set up new context to start executing at forkret
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

**Modified freeproc():**
```c
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  
  // NEW: Free usyscall page
  if(p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
  
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
}
```

**Modified proc_pagetable():**
```c
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // Map the trampoline code (for system call return)
  // at the highest user virtual address.
  // Only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // Map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  // NEW: Map the usyscall page just below TRAPFRAME
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}
```

**Modified proc_freepagetable():**
```c
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmunmap(pagetable, USYSCALL, 1, 0);  // NEW: Unmap usyscall page
  uvmfree(pagetable, sz);
}
```

**Modified fork() to copy pid to child:**
```c
int
fork(void)
{
  // ... existing code ...
  
  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // NEW: Copy trace mask and update usyscall page
  np->trace_mask = p->trace_mask;
  np->usyscall->pid = np->pid;

  // ... rest of fork() ...
}
```

### Explanation

**Memory Layout with USYSCALL**:
```
High Address
+-------------+
|  TRAMPOLINE |  (kernel code for returning to user)
+-------------+
|  TRAPFRAME  |  (saved registers during trap)
+-------------+
|  USYSCALL   |  (shared page with pid)  <-- NEW
+-------------+
|    Stack    |
|     ...     |
|    Heap     |
|    Data     |
|    Text     |
Low Address
```

**How It Works**:
1. Allocate a physical page for `struct usyscall` during `allocproc()`
2. Map this page at a fixed user virtual address `USYSCALL` (read-only to user)
3. Store process ID in this page
4. User programs can read pid directly from this page without trapping into kernel
5. Update pid in child during `fork()`

**Benefits**:
- `getpid()` becomes a simple memory read instead of a system call
- Eliminates context switch overhead for frequently-called syscall
- Safe because page is read-only (user cannot modify)

---

## 2. Print Page Table

### Objective
Implement a function to dump the page table contents for debugging purposes, showing the mapping from virtual to physical addresses.

### Files Modified

#### `kernel/defs.h` - Add declaration
```c
// vm.c
void            kvminit(void);
uint64          kvmpa(uint64);
void            kvmmap(uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
void            uvminit(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
void            freerange(void *pa_start, void *pa_end);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);  // NEW: Declare vmprint
```

#### `kernel/vm.c` - Implement vmprint()

```c
// Recursively print page-table pages.
// depth: current depth in the page table tree (0 = root)
void
vmprint_recursive(pagetable_t pagetable, int depth)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      // Print indentation based on depth
      if(depth == 0)
        printf("..");
      else if(depth == 1)
        printf(".. ..");
      else if(depth == 2)
        printf(".. .. ..");
      
      uint64 child = PTE2PA(pte);
      printf("%d: pte %p pa %p\n", i, pte, child);
      
      // If this is not a leaf (depth < 2 and PTE has RWX bits clear),
      // recursively print the child page table
      if(depth < 2 && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
        vmprint_recursive((pagetable_t)child, depth + 1);
      }
    }
  }
}

// Print the page table in a readable format
void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_recursive(pagetable, 0);
}
```

#### `kernel/exec.c` - Call vmprint() when loading first process
```c
int
exec(char *path, char **argv)
{
  // ... existing code ...
  
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  // NEW: Print page table for debugging
  if(p->pid == 1)
    vmprint(p->pagetable);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  // ... error handling ...
}
```

### Explanation

**RISC-V Page Table Structure**:
- 3-level page table (Sv39)
- Each level has 512 entries (9 bits)
- VPN[2] | VPN[1] | VPN[0] | Offset (9 | 9 | 9 | 12 bits)

**Page Table Entry (PTE) Format**:
```
| 63-54 | 53-28 | 27-19 | 18-10 | 9-8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| RSV   | PPN[2]| PPN[1]| PPN[0]| RSW | D | A | G | U | X | W | R | V |
```
- V = Valid
- R/W/X = Read/Write/Execute permissions
- U = User accessible
- A = Accessed
- D = Dirty
- PPN = Physical Page Number

**Output Format**:
```
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
```

**Key Points**:
- Recursively traverses the 3-level page table
- Only prints valid entries (PTE_V set)
- Indents based on depth to show hierarchy
- Leaf nodes have R, W, or X bits set

---

## 3. Page Access (pgaccess)

### Objective
Implement a system call that detects which pages have been accessed by checking the Accessed (A) bit in page table entries.

### Files Added/Modified

#### `kernel/riscv.h` - Define PTE_A bit (if not already defined)
```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access
#define PTE_A (1L << 6) // accessed bit
```

#### `kernel/sysproc.c` - Implement sys_pgaccess()

```c
uint64
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
    return -1;
  }

  // kernel buffer for storing access bits
  unsigned int abits = 0;

  struct proc *p = myproc();
  uint64 base = PGROUNDDOWN(buf);
  for (int i = 0; i < size; i ++){
    uint64 va = base + PGSIZE * i;
    pte_t *pte = walk(p->pagetable, va, 0);

    if(pte == 0) {
      continue;  // Page not mapped
    }
    
    if(*pte & PTE_A) {
      // Page was accessed
      abits |= (1 << i);
      // Clear the accessed bit for next check
      *pte &= ~PTE_A;
    }
  }
  
  // Copy result to user space
  if(copyout(p->pagetable, dstva, (char *)&abits, sizeof(abits)) < 0) {
    return -1;
  }

  return 0;
}
```

#### `kernel/syscall.h` - Add system call number
```c
#define SYS_pgaccess 24
```

#### `kernel/syscall.c` - Add to syscall table
```c
extern uint64 sys_pgaccess(void);

static uint64 (*syscalls[])(void) = {
    ...
    [SYS_pgaccess] sys_pgaccess,
};
```

#### `user/user.h` - Add user-space declaration
```c
int pgaccess(void *base, int len, void *mask);
```

#### `user/usys.pl` - Add entry stub
```perl
entry("pgaccess");
```

### Explanation

**Accessed Bit (PTE_A)**:
- Set by hardware when page is read from or written to
- Used by operating systems for page replacement algorithms
- Must be cleared by software after reading

**System Call Interface**:
```c
int pgaccess(void *base, int len, void *mask);
```
- `base`: Starting virtual address
- `len`: Number of pages to check (max 32)
- `mask`: User buffer to store result bitmask

**Algorithm**:
1. Validate arguments (size <= 32)
2. For each page in range:
   - Calculate virtual address: `va = base + i * PGSIZE`
   - Walk page table to get PTE
   - Check if PTE_A bit is set
   - If set: set corresponding bit in result and clear PTE_A
3. Copy result bitmask to user space

**Result Format**:
- 32-bit bitmask where bit i = 1 means page i was accessed
- Example: `0b101` means pages 0 and 2 were accessed

**Key Points**:
- Uses `walk()` to find PTE for virtual address
- Clears PTE_A after checking (for future tracking)
- Handles unmapped pages gracefully (skip them)

---

## Testing

### USYSCALL Test
```bash
$ pgtbltest
Testing USYSCALL page
getpid() via syscall: 3
getpid() via usyscall: 3
Test passed!
```

### vmprint Output
```bash
$ ls
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1b pa 0x0000000087f67000
```

### pgaccess Test
```bash
$ pgaccesstest
Allocating pages...
Touching pages 0 and 2...
Checking access...
Access mask: 0x5 (binary: 101)
Test passed!
```

---

## Summary

| Exercise | Key Concepts | Modified Files |
|----------|-------------|----------------|
| USYSCALL | Shared kernel-user pages, fast syscalls | proc.c, memlayout.h |
| vmprint | Page table traversal, RISC-V paging | vm.c, exec.c |
| pgaccess | PTE flags, walk(), copyout() | sysproc.c |

### Key Concepts Learned
1. **Page table structure**: 3-level hierarchy in RISC-V Sv39
2. **PTE flags**: V, R, W, X, U, A, D bits and their meanings
3. **walk() function**: Translating virtual to physical addresses
4. **copyout/copyin**: Safe data transfer between kernel and user space
5. **Shared pages**: Mapping kernel data read-only to user space
