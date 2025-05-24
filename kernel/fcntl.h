#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
// When a process specifies O_NOFOLLOW in the flags to open, open
// should open the symlink (and not follow the symbolic link).
#define O_NOFOLLOW 0x500