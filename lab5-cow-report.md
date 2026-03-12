# Lab 5: Copy-on-Write Fork - Implementation Report

## Overview
This lab implements copy-on-write (COW) fork to optimize memory usage and improve fork performance. Instead of copying all memory pages when forking, parent and child share physical pages until one of them writes to a page.

---

## Key Concepts

**Copy-on-Write (COW)**:
- When `fork()` is called, parent and child share the same physical pages
- Pages are marked read-only and with a special COW flag
- When either process tries to write, a page fault occurs
- The kernel then copies the page and gives each process its own writable copy
- Saves memory and makes `fork()` faster

**Reference Counting**:
- Track how many processes share each physical page
- Only free a page when its reference count reaches zero
- Prevents premature deallocation of shared pages

---

## Files Modified

### 1. Reference Counting for Physical Pages

#### `kernel/kalloc.c` - Add reference counting

```c
struct {
  struct spinlock lock;
  struct run *freelist;
  uint64 ref_counts[(PHYSTOP - KERNBASE) / PGSIZE];  // NEW: Reference count array
} kmem;

// Get index into ref_counts for a physical address
#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmem.ref_counts[PA2IDX(p)] = 1;  // Initialize ref count to 1
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc().
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  
  // NEW: Decrement reference count, only free if zero
  uint64 idx = PA2IDX(pa);
  if(kmem.ref_counts[idx] > 1) {
    kmem.ref_counts[idx]--;
    release(&kmem.lock);
    return;
  }
  
  kmem.ref_counts[idx] = 0;
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    kmem.ref_counts[PA2IDX(r)] = 1;  // NEW: Initialize ref count to 1
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// NEW: Increment reference count for a physical page
void
inc_ref_count(uint64 pa)
{
  acquire(&kmem.lock);
  kmem.ref_counts[PA2IDX(pa)]++;
  release(&kmem.lock);
}

// NEW: Get reference count for a physical page
int
get_ref_count(uint64 pa)
{
  int count;
  acquire(&kmem.lock);
  count = kmem.ref_counts[PA2IDX(pa)];
  release(&kmem.lock);
  return count;
}
```

### 2. COW Page Table Entry Flag

#### `kernel/riscv.h` - Define PTE_COW flag
```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access
#define PTE_G (1L << 5)
#define PTE_A (1L << 6);
#define PTE_D (1L << 7);
#define PTE_COW (1L << 8)  // NEW: Copy-on-Write flag (RSW bit)
```

### 3. Modified uvmcopy for COW

#### `kernel/vm.c` - Copy-on-Write fork implementation

```c
// Copies both the page table and the physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not valid");
    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // NEW: Increment reference count for shared page
    inc_ref_count(pa);

    // If page is writable, make it COW (read-only with COW flag)
    if(flags & PTE_W) {
      // Remove write permission and add COW flag
      flags = (flags & ~PTE_W) | PTE_COW;
      // Update parent PTE to also be COW
      *pte = (*pte & ~PTE_W) | PTE_COW;
    }
    
    // Map the page in child's page table (shared physical page)
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      printf("uvmcopy: mappages failed\n");
      kfree((void *)pa);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

### 4. COW Page Fault Handler

#### `kernel/vm.c` - Helper function and COW handler

```c
// Get physical address for a virtual address
uint64
get_pa(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if(!pte){
    printf("get_pa(): pte is null\n");
    return 0;
  }
  uint64 pa = PTE2PA(*pte);
  uint64 flags = PTE_FLAGS(*pte);
  
  if (!(flags & PTE_U) || !(flags & PTE_V)) {
    panic("get_pa(): user or valid bit not set\n");
  }
  return pa;
}

// Handle Copy-on-Write page fault
// Returns 0 on success, -1 on failure
int
cow_handler(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa, newpa;
  uint flags;
  char *mem;

  if(va >= MAXVA)
    return -1;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return -1;
  if((*pte & PTE_V) == 0)
    return -1;
  if((*pte & PTE_U) == 0)
    return -1;
  
  // Check if this is a COW page
  if(!(*pte & PTE_COW))
    return -1;

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);
  
  // Get reference count for this physical page
  int ref_count = get_ref_count(pa);
  
  if(ref_count == 1) {
    // Only one reference, just make it writable
    *pte = (*pte & ~PTE_COW) | PTE_W;
  } else {
    // Multiple references, need to copy
    if((mem = kalloc()) == 0)
      return -1;  // Out of memory
    
    // Copy content from shared page
    memmove(mem, (char*)pa, PGSIZE);
    
    // Decrement reference count of old page
    kfree((void*)pa);
    
    // Update PTE to point to new page with write permission
    newpa = (uint64)mem;
    flags = (flags & ~PTE_COW) | PTE_W;
    *pte = PA2PTE(newpa) | flags;
  }
  
  return 0;
}
```

### 5. Modified Trap Handler

#### `kernel/trap.c` - Handle COW page faults in usertrap()

```c
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

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
  } else if(r_scause() == 13 || r_scause() == 15) {
    // NEW: Handle store/amo page fault (COW)
    uint64 va = r_stval();  // Faulting virtual address
    if(cow_handler(p->pagetable, va) < 0) {
      // COW handler failed, kill process
      printf("usertrap(): COW handler failed, killing process %d\n", p->pid);
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  if(which_dev == 2){
    yield();
  }

  usertrapret();
}
```

### 6. Modified copyout for COW

#### `kernel/vm.c` - Handle COW in copyout()

```c
// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    
    // NEW: Handle COW pages during copyout
    pte = walk(pagetable, va0, 0);
    if(pte && (*pte & PTE_COW)) {
      if(cow_handler(pagetable, va0) < 0)
        return -1;
    }
    
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

### 7. Header Declarations

#### `kernel/defs.h` - Add declarations
```c
// kalloc.c
void*           kalloc(void);
void            kfree(void *);
void            kinit(void);
void            inc_ref_count(uint64);  // NEW
int             get_ref_count(uint64);  // NEW

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
uint64          get_pa(pagetable_t, uint64);  // NEW
int             cow_handler(pagetable_t, uint64);  // NEW
void            freerange(void *pa_start, void *pa_end);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
void            vmprint(pagetable_t);
```

---

## Explanation

### COW Fork Flow

```
1. Process A calls fork()
   
   Before fork:
   +----------------+      +----------------+
   | Process A      |      | Physical Page  |
   | PTE: W=1, V=1  |----->| (Read/Write)   |
   +----------------+      +----------------+

2. During uvmcopy():
   - Increment ref count of shared page
   - Mark both parent and child PTE as COW (W=0, COW=1)
   
   After fork:
   +----------------+      +----------------+
   | Process A      |      | Physical Page  |
   | PTE: W=0,COW=1 |----->| (Shared)       |
   +----------------+      | ref_count = 2  |
   | Process B      |      +----------------+
   | PTE: W=0,COW=1 |------^
   +----------------+

3. Process B writes to page:
   - Page fault (scause = 15, store page fault)
   - COW handler triggered
   - Allocate new page, copy content
   - Decrement ref count of old page
   - Map new page with W=1
   
   After write:
   +----------------+      +----------------+
   | Process A      |      | Physical Page 1|
   | PTE: W=0,COW=1 |----->| (Shared)       |
   +----------------+      | ref_count = 1  |
                           +----------------+
   +----------------+      +----------------+
   | Process B      |      | Physical Page 2|
   | PTE: W=1,V=1   |----->| (Private)      |
   +----------------+      +----------------+
```

### Reference Counting

**Array Indexing**:
```
Physical Address Space:
KERNBASE (0x80000000)          PHYSTOP (0x88000000)
|--------------------------------|
  ^                              ^
  |                              |
idx = 0                    idx = (PHYSTOP-KERNBASE)/PGSIZE
```

**Operations**:
- `kalloc()`: Set ref_count to 1
- `uvmcopy()`: Increment ref_count
- `cow_handler()`: Decrement old page, new page starts at 1
- `kfree()`: Decrement ref_count, free only when 0

### Page Fault Causes

| scause | Description | Action |
|--------|-------------|--------|
| 12 | Instruction page fault | Kill process |
| 13 | Load page fault | Check for COW |
| 15 | Store/AMO page fault | Handle COW if PTE_COW set |

---

## Testing

```bash
$ cowtest
simple: ok
three: ok
file: ok
ALL COW TESTS PASSED

$ usertests
... all tests pass ...
```

### Test Cases

1. **simple**: Basic COW fork and write
2. **three**: Multiple forks with COW
3. **file**: COW with file operations

---

## Summary

| Component | Purpose | Key Functions |
|-----------|---------|---------------|
| Reference Counting | Track shared pages | `inc_ref_count()`, `get_ref_count()`, modified `kfree()` |
| PTE_COW Flag | Mark COW pages | Added to `riscv.h` |
| uvmcopy() | Share pages instead of copy | Modified to set COW flag and share pages |
| cow_handler() | Handle COW page faults | Allocates new page on write, copies content |
| usertrap() | Detect COW faults | Check scause 13/15, call cow_handler() |
| copyout() | Handle COW during kernel copy | Check and handle COW pages |

### Key Concepts Learned
1. **Copy-on-Write**: Sharing pages until write occurs
2. **Reference Counting**: Tracking page usage to prevent premature freeing
3. **Page Fault Handling**: Using exceptions to implement COW
4. **PTE Flags**: Custom flags in reserved bits
5. **Race Conditions**: Proper locking in kalloc/kfree
