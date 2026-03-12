# Lab 1: Utilities - Implementation Report

## Overview
This lab focuses on familiarizing with xv6 user-space programming by implementing basic Unix utilities. The implementations demonstrate process creation, inter-process communication via pipes, and file system operations.

---

## 1. Sleep

### File: `user/sleep.c`

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: Sleep t, where t is the time to sleep in seconds.\n");
        exit(-1);
    }
    int t = atoi(argv[1]);
    sleep(t);
    exit(0);
}
```

### Explanation
- **Argument validation**: Checks if exactly one argument (sleep time) is provided
- **String to integer conversion**: Uses `atoi()` to convert command-line argument to integer
- **System call**: Calls `sleep(t)` to pause execution for `t` clock ticks
- **Exit**: Returns 0 on success, -1 on error

### Key Concepts
- Command-line argument handling in xv6
- Using the `sleep()` system call
- Proper error handling and exit codes

---

## 2. PingPong

### File: `user/pingpong.c`

```c
#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int p1[2];
    pipe(p1); // parent to child
    int p2[2];
    pipe(p2);        // child to parent
    if (fork() == 0) // child
    {
        char bufRead[100];
        if (read(p1[0], bufRead, 100) > 0)
        {
            if (strcmp(bufRead, "114514") == 0)
            {
                fprintf(1, "%d: received ping\n", getpid());
            }
            else
            {
                fprintf(1, "%d: received wrong ping message\n", getpid());
            }
        }
        else
        {
            exit(-1);
        }
        char *bufWrite = "114514";
        if (write(p2[1], bufWrite, strlen(bufWrite)) == strlen(bufWrite))
        {
            exit(0);
        }
    }
    else // parent
    {
        char *bufWrite = "114514";
        if (write(p1[1], bufWrite, strlen(bufWrite)) == strlen(bufWrite))
        {
            char bufRead[100];
            if (read(p2[0], bufRead, 100) > 0)
            {
                if (strcmp(bufRead, "114514") == 0)
                {
                    fprintf(1, "%d: received pong\n", getpid());
                }
                else
                {
                    fprintf(1, "%d: received wrong ping message\n", getpid());
                }
            }
            else
            {
                exit(-1);
            }
        }
    }
    exit(0);
}
```

### Explanation
1. **Pipe Creation**: Two pipes are created:
   - `p1`: Parent → Child communication
   - `p2`: Child → Parent communication

2. **Child Process** (`fork() == 0`):
   - Reads message from parent via `p1[0]` (read end of pipe 1)
   - Validates the message ("114514")
   - Prints: `<pid>: received ping`
   - Sends response to parent via `p2[1]` (write end of pipe 2)

3. **Parent Process**:
   - Sends message to child via `p1[1]` (write end of pipe 1)
   - Reads response from child via `p2[0]` (read end of pipe 2)
   - Validates the message
   - Prints: `<pid>: received pong`

### Key Concepts
- **Pipes**: Unidirectional communication channels between processes
- `pipe(fd)` creates a pipe; `fd[0]` is read end, `fd[1]` is write end
- **Fork**: Creates a child process that is an exact copy of the parent
- Each process has its own copy of file descriptors, but they point to the same underlying pipe

---

## 3. Primes (Sieve of Eratosthenes)

### File: `user/primes.c`

```c
#include "kernel/types.h"
#include "user/user.h"

// p is the input pipe
int sieve(int *p)
{
    int prime;
    int res;
    int po[2];
    pipe(po);
    read(p[0], &prime, sizeof(prime));
    fprintf(1, "prime %d\n", prime);
    int childCreated = 0;
    int poClosed = 0;
    while (read(p[0], &res, sizeof(res)) > 0)
    {
        if (!childCreated)
        {
            childCreated = 1;
            if (fork() == 0)
            {
                close(p[0]);
                close(po[1]);
                sieve(po);
            }
        }
        if (!poClosed)
        {
            close(po[0]);
            poClosed = 1;
        }
        if (res % prime != 0)
        {
            write(po[1], &res, sizeof(res));
        }
    }
    close(po[1]);
    wait(0);
    exit(0);
};

int main()
{
    int p[2];
    pipe(p);
    if (fork() == 0)
    {
        close(p[1]);
        sieve(p);
    }
    else
    {
        // no need to read
        close(p[0]);
        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(i));
        }
        // write finished
        close(p[1]);
        wait(0);
        exit(0);
    }
    return 0;
};
```

### Explanation
This implements the **Sieve of Eratosthenes** using a pipeline of processes.

**Algorithm Flow**:
1. **Main Process**: Creates initial pipe and feeds numbers 2-35 into it
2. **Recursive Pipeline**: Each process in the chain:
   - Reads the first number (always a prime)
   - Prints the prime
   - Creates a new pipe and forks a child
   - Filters numbers: only passes numbers NOT divisible by its prime to the next stage

**Key Design Decisions**:
- `childCreated` flag ensures only one child is forked per process
- `poClosed` flag ensures the read end of the output pipe is closed after forking
- Each process waits for its child to complete (`wait(0)`) before exiting

**Process Pipeline**:
```
Main(2-35) → P1(2) → P2(3) → P3(5) → P4(7) → ...
             [filt]   [filt]   [filt]   [filt]
```

### Key Concepts
- **Pipeline architecture**: Data flows through a chain of processes
- **Recursive process creation**: Each stage spawns the next stage
- **Lazy child creation**: Child is only created when needed (when there's data to process)
- **Resource cleanup**: Properly closing unused pipe ends to prevent hangs

---

## 4. Find

### File: `user/find.c`

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *extractFileNameFromPath(char *path)
{
    char *p = path + strlen(path);
    while (p >= path && *p != '/')
    {
        p--;
    }
    return p + 1;
}

void find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE:
        // fprintf(1, "file branch.\n path = %s\n fmtname(path) = %s\n target=%s\n", path, fmtname(path), target);
        if (strcmp(extractFileNameFromPath(path), target) == 0)
        {
            printf("%s\n", path);
        }
        break;

    case T_DIR:
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf))
        {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0)
                continue;
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0)
            {
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            find(buf, target);
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(2, "usage: find {path} {file name}\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
```

### Explanation

**Helper Function - `extractFileNameFromPath`**:
- Extracts the filename from a full path by finding the last '/'
- Returns pointer to the character after the last slash

**Main Function - `find`**:
- Recursively traverses directory tree starting from `path`
- Searches for files matching `target` name

**Directory Entry Structure** (`struct dirent`):
```c
struct dirent {
    ushort inum;    // Inode number (0 = unused entry)
    char name[DIRSIZ];  // Filename (14 bytes)
};
```

**File Type Handling**:
1. **T_FILE**: Regular file
   - Compare filename with target
   - Print full path if match found

2. **T_DIR**: Directory
   - Build full path by appending `/` and entry name
   - Skip `.` and `..` to avoid infinite recursion
   - Recursively call `find()` on subdirectories

**Key Safety Checks**:
- Path length validation to prevent buffer overflow
- Check for failed `open()` and `stat()` calls
- Skip entries with `inum == 0` (unused directory entries)

### Key Concepts
- **Directory traversal**: Using `read()` on directory file descriptors
- **Path construction**: Building full paths from directory entries
- **Recursive search**: Depth-first traversal of directory tree
- **Inode numbers**: Using `de.inum` to identify valid entries

---

## Testing

Run tests for each utility:

```bash
# Sleep - pause for 10 ticks
$ sleep 10

# PingPong - test IPC
$ pingpong
3: received ping
4: received pong

# Primes - generate primes up to 35
$ primes
prime 2
prime 3
prime 5
...

# Find - search for files
$ find . README
./README
```

---

## Summary

| Utility | Key System Calls | Concepts Demonstrated |
|---------|------------------|----------------------|
| sleep | `sleep()`, `exit()` | Basic system calls |
| pingpong | `pipe()`, `fork()`, `read()`, `write()`, `getpid()` | IPC with pipes |
| primes | `pipe()`, `fork()`, `wait()` | Pipeline architecture |
| find | `open()`, `read()`, `stat()`, `close()` | File system traversal |
