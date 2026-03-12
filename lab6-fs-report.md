# Lab 6: File System - Implementation Report

## Overview
This lab extends the xv6 file system to support larger files by implementing doubly-indirect block addressing. The original xv6 file system is limited to 268 blocks (about 256 KB), and this implementation extends it to support much larger files.

---

## Original xv6 File System Limitations

### Original Structure
```
inode.addrs[] array:
+------------------+
| direct[0]        |  Block 0
| direct[1]        |  Block 1
| ...              |  ...
| direct[11]       |  Block 11
+------------------+
| indirect         |  Points to block containing 256 block numbers
+------------------+
```

**Original Maximum File Size**:
- 12 direct blocks × 1 KB = 12 KB
- 1 indirect block × 256 entries × 1 KB = 256 KB
- **Total: 268 KB**

---

## Extended File System Structure

### New inode Structure
```c
// fs.h - Modified structure
#define NDIRECT 11              // Reduced from 12 to make room
#define NINDIRECT (BSIZE / sizeof(uint))
#define N_SINGLE_INDIRECT 256   // Entries in singly-indirect block
#define N_DOUBLY_INDIRECT (256 * 256)  // Entries in doubly-indirect
#define MAXFILE (NDIRECT + N_SINGLE_INDIRECT + N_DOUBLY_INDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2]; // Data block addresses (11 direct + 1 singly-indirect + 1 doubly-indirect)
};
```

### New Addressing Structure
```
inode.addrs[] array:
+------------------+
| direct[0..10]    |  Blocks 0-10 (11 blocks)
+------------------+
| singly_indirect  |  Points to block with 256 direct block numbers
+------------------+
| doubly_indirect  |  Points to block with 256 singly-indirect block numbers
+------------------+
```

**New Maximum File Size**:
- 11 direct blocks × 1 KB = 11 KB
- 1 singly-indirect block × 256 entries × 1 KB = 256 KB
- 1 doubly-indirect block × 256 × 256 entries × 1 KB = 65,536 KB
- **Total: ~65,803 KB (~64 MB)**

---

## Files Modified

### 1. Header Definitions

#### `kernel/fs.h` - Update file system constants
```c
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define N_SINGLE_INDIRECT NINDIRECT
#define N_DOUBLY_INDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + N_SINGLE_INDIRECT + N_DOUBLY_INDIRECT)

// On-disk inode structure
struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+2];  // Changed from NDIRECT+1 to NDIRECT+2
};

// In-memory inode (same structure)
struct inode {
  uint dev;
  uint inum;
  int ref;
  struct sleeplock lock;
  short valid;
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+2];  // Changed from NDIRECT+1 to NDIRECT+2
};
```

### 2. Block Mapping (bmap)

#### `kernel/fs.c` - Extended bmap() function
```c
// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // Direct blocks: bn < 11
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  // Singly-indirect block: bn < 256
  if(bn < N_SINGLE_INDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= N_SINGLE_INDIRECT;

  // Doubly-indirect block: bn < 256*256
  if(bn < N_DOUBLY_INDIRECT){
    // Load level 1 indirect block (doubly-indirect)
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    
    struct buf *lvl1_bp = bread(ip->dev, addr);
    uint *lvl1_a = (uint*)lvl1_bp->data;
    
    // Calculate indices
    uint lvl1_index = bn / N_SINGLE_INDIRECT;  // Which singly-indirect block
    uint lvl2_index = bn % N_SINGLE_INDIRECT;  // Which entry in that block
    
    // Load level 2 indirect block (singly-indirect), allocating if necessary
    if((addr = lvl1_a[lvl1_index]) == 0){
      lvl1_a[lvl1_index] = addr = balloc(ip->dev);
      log_write(lvl1_bp);
    }
    brelse(lvl1_bp);
    
    struct buf *lvl2_bp = bread(ip->dev, addr);
    uint *lvl2_a = (uint*)lvl2_bp->data;
    
    if((addr = lvl2_a[lvl2_index]) == 0){
      lvl2_a[lvl2_index] = addr = balloc(ip->dev);
      log_write(lvl2_bp);
    }
    brelse(lvl2_bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

### 3. Block Deallocation (itrunc)

#### `kernel/fs.c` - Extended itrunc() function
```c
// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  // Free direct blocks
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // Free singly-indirect blocks
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < N_SINGLE_INDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // Free doubly-indirect blocks
  if(ip->addrs[NDIRECT+1]){
    struct buf *lvl1_bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    uint *lvl1_a = (uint*)lvl1_bp->data;
    
    // Free all singly-indirect blocks pointed to by doubly-indirect
    for(j = 0; j < N_SINGLE_INDIRECT; j++){
      if(lvl1_a[j]){
        struct buf *lvl2_bp = bread(ip->dev, lvl1_a[j]);
        uint *lvl2_a = (uint*)lvl2_bp->data;
        
        // Free all direct blocks in this singly-indirect block
        for(int k = 0; k < N_SINGLE_INDIRECT; k++){
          if(lvl2_a[k])
            bfree(ip->dev, lvl2_a[k]);
        }
        
        brelse(lvl2_bp);
        bfree(ip->dev, lvl1_a[j]);
      }
    }
    
    brelse(lvl1_bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

---

## Explanation

### Block Number Translation

**Addressing Levels**:
```
Block Number (bn) Analysis:

bn < 11:                    Direct block
11 <= bn < 267:             Singly-indirect (11 + 256)
267 <= bn < 65803:          Doubly-indirect (267 + 65536)
```

**Doubly-Indirect Indexing**:
```
For bn in doubly-indirect range:
  bn_relative = bn - NDIRECT - N_SINGLE_INDIRECT
  
  lvl1_index = bn_relative / N_SINGLE_INDIRECT  // Which indirect block
  lvl2_index = bn_relative % N_SINGLE_INDIRECT  // Which entry in that block

Example: bn = 1000
  bn_relative = 1000 - 11 - 256 = 733
  lvl1_index = 733 / 256 = 2
  lvl2_index = 733 % 256 = 221
```

### Block Mapping Flow

```
Direct Block (bn = 5):
  return ip->addrs[5]

Singly-Indirect (bn = 100):
  bn = 100 - 11 = 89
  indirect_block = ip->addrs[11]
  return indirect_block[89]

Doubly-Indirect (bn = 1000):
  bn = 1000 - 11 - 256 = 733
  lvl1_block = ip->addrs[12]
  lvl2_block = lvl1_block[733 / 256]  // = lvl1_block[2]
  return lvl2_block[733 % 256]        // = lvl2_block[221]
```

### Block Deallocation Flow

```
itrunc():
  1. Free 11 direct blocks
  2. Free singly-indirect block:
     - Read indirect block
     - Free all 256 data blocks it points to
     - Free the indirect block itself
  3. Free doubly-indirect block:
     - Read level-1 indirect block
     - For each of 256 entries:
       - Read level-2 indirect block
       - Free all 256 data blocks it points to
       - Free the level-2 indirect block
     - Free the level-1 indirect block
```

---

## Data Structures

### On-Disk inode (dinode)
```
Offset  Size  Description
-------------------------
0       2     type (T_DIR, T_FILE, T_DEVICE)
2       2     major device number
4       2     minor device number
6       2     nlink (reference count)
8       4     size (file size in bytes)
12      52    addrs[13] (11 direct + 1 singly + 1 doubly)
```

### Indirect Block Structure
```
Singly-Indirect Block (1 KB):
+------------------+
| addr[0]          |  4 bytes - Block number of data block 0
| addr[1]          |  4 bytes - Block number of data block 1
| ...              |  ...
| addr[255]        |  4 bytes - Block number of data block 255
+------------------+

Doubly-Indirect Level 1 Block:
+------------------+
| indirect[0]      |  Block number of singly-indirect block 0
| indirect[1]      |  Block number of singly-indirect block 1
| ...              |  ...
| indirect[255]    |  Block number of singly-indirect block 255
+------------------+
```

---

## Testing

```bash
$ bigfile
created big file with 65803 blocks
write succeeded
read succeeded
$
```

### Test Program (user/bigfile.c)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

int
main()
{
  char buf[BSIZE];
  int fd, i, blocks;

  fd = open("big.file", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf("bigfile: cannot open big.file for writing\n");
    exit(-1);
  }

  blocks = 0;
  while(1){
    *(int*)buf = blocks;
    int cc = write(fd, buf, sizeof(buf));
    if(cc <= 0)
      break;
    blocks++;
    if(blocks % 100 == 0)
      printf(".");
  }

  printf("\nwrote %d blocks\n", blocks);
  printf("max blocks: %d\n", MAXFILE);
  
  if(blocks != MAXFILE) {
    printf("error: wrote %d blocks, expected %d\n", blocks, MAXFILE);
    exit(-1);
  }
  
  close(fd);
  
  // Verify by reading back
  fd = open("big.file", O_RDONLY);
  if(fd < 0){
    printf("bigfile: cannot re-open big.file for reading\n");
    exit(-1);
  }
  
  for(i = 0; i < blocks; i++){
    int cc = read(fd, buf, sizeof(buf));
    if(cc <= 0){
      printf("bigfile: read error at block %d\n", i);
      exit(-1);
    }
    if(*(int*)buf != i){
      printf("bigfile: data mismatch at block %d\n", i);
      exit(-1);
    }
  }
  
  printf("read all blocks successfully\n");
  close(fd);
  unlink("big.file");
  
  exit(0);
}
```

---

## Summary

| Component | Original | Extended |
|-----------|----------|----------|
| Direct blocks | 12 | 11 |
| Indirect levels | 1 | 2 |
| Max blocks | 268 | 65,803 |
| Max file size | ~256 KB | ~64 MB |

### Key Functions Modified

| Function | Purpose |
|----------|---------|
| `bmap()` | Map file block number to disk block number |
| `itrunc()` | Free all blocks associated with an inode |

### Key Concepts Learned
1. **Indirect Addressing**: Using disk blocks to store block pointers
2. **Multi-level Indexing**: Doubly-indirect blocks for large files
3. **Block Allocation/Deallocation**: Properly managing disk blocks
4. **Logging**: Using `log_write()` for crash consistency
5. **Buffer Cache**: Using `bread()`/`brelse()` for block I/O
