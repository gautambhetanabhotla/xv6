#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

// avoid clashes with host struct stat / struct dirent
#define stat xv6_stat
#define dirent xv6_dirent
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#undef stat
#undef dirent

#ifndef static_assert
#define static_assert(a, b)                                                    \
  do {                                                                         \
    switch (0)                                                                 \
    case 0:                                                                    \
    case (a):;                                                                 \
  } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE / BPB + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGBLOCKS + 1; // Header followed by LOGBLOCKS data blocks.
int nmeta;   // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks; // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;

void balloc(int);
void wsect(uint, void *);
void winode(uint, struct dinode *);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);
void addtree(uint dirino, const char *hostdir);
static void padsize(uint inum);

// convert to riscv byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar *)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar *)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i;
  uint rootino;
  struct xv6_dirent de;
  char buf[BSIZE];

  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if (argc != 3) {
    fprintf(stderr, "Usage: mkfs fs.img rootdir\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct xv6_dirent)) == 0);

  fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fsfd < 0)
    die(argv[1]);

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  sb.magic = FSMAGIC;
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2 + nlog);
  sb.bmapstart = xint(2 + nlog + ninodeblocks);

  printf(
    "nmeta %d (boot, super, log blocks %u, inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
    nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta; // the first free block that we can allocate

  for (i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  addtree(rootino, argv[2]);
  padsize(rootino);

  balloc(freeblock);

  exit(0);
}

// Round a directory's size up to a whole block, as the original mkfs
// did for the root directory.
static void
padsize(uint inum)
{
  struct dinode din;
  uint off;

  rinode(inum, &din);
  off = xint(din.size);
  off = ((off / BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(inum, &din);
}

static void
adddirent(uint dirino, uint inum, const char *name)
{
  struct xv6_dirent de;

  bzero(&de, sizeof(de));
  de.inum = xshort(inum);
  strncpy(de.name, name, DIRSIZ);
  iappend(dirino, &de, sizeof(de));
}

static int
skipdots(const struct dirent *d)
{
  return strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0;
}

// Recursively copy the contents of host directory hostdir into the
// xv6 directory dirino. Regular files, directories and symbolic links
// are supported; anything else is skipped with a warning.
void
addtree(uint dirino, const char *hostdir)
{
  struct dirent **ents;
  char path[PATH_MAX], buf[BSIZE];
  struct stat st;
  struct dinode din;
  uint inum;
  int i, n, fd, cc;
  ssize_t len;

  // Sort entries so the resulting image is deterministic.
  n = scandir(hostdir, &ents, skipdots, alphasort);
  if (n < 0)
    die(hostdir);

  for (i = 0; i < n; i++) {
    const char *name = ents[i]->d_name;

    if (snprintf(path, sizeof(path), "%s/%s", hostdir, name) >= sizeof(path)) {
      fprintf(stderr, "mkfs: path too long: %s/%s\n", hostdir, name);
      exit(1);
    }
    if (strlen(name) > DIRSIZ) {
      fprintf(stderr, "mkfs: name longer than %d chars: %s\n", DIRSIZ, path);
      exit(1);
    }
    if (lstat(path, &st) < 0)
      die(path);

    if (S_ISDIR(st.st_mode)) {
      inum = ialloc(T_DIR);
      adddirent(dirino, inum, name);
      adddirent(inum, inum, ".");
      adddirent(inum, dirino, "..");
      // ".." in the new directory is a link to the parent.
      rinode(dirino, &din);
      din.nlink = xshort(xshort(din.nlink) + 1);
      winode(dirino, &din);
      addtree(inum, path);
      padsize(inum);
    } else if (S_ISREG(st.st_mode)) {
      inum = ialloc(T_FILE);
      adddirent(dirino, inum, name);
      if ((fd = open(path, O_RDONLY)) < 0)
        die(path);
      while ((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);
      if (cc < 0)
        die(path);
      close(fd);
    } else if (S_ISLNK(st.st_mode)) {
      // The kernel stores the target NUL-terminated (see sys_symlink).
      if ((len = readlink(path, buf, MAXPATH)) < 0)
        die(path);
      if (len >= MAXPATH) {
        fprintf(stderr, "mkfs: symlink target too long: %s\n", path);
        exit(1);
      }
      buf[len] = '\0';
      inum = ialloc(T_SYMLINK);
      adddirent(dirino, inum, name);
      iappend(inum, buf, len + 1);
    } else {
      fprintf(stderr, "mkfs: skipping unsupported file type: %s\n", path);
    }
    free(ents[i]);
  }
  free(ents);
}

void
wsect(uint sec, void *buf)
{
  if (lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if (write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode *)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode *)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if (lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if (read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  if (inum >= NINODES) {
    fprintf(stderr, "mkfs: out of inodes (NINODES = %d)\n", NINODES);
    exit(1);
  }

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BPB);
  bzero(buf, BSIZE);
  for (i = 0; i < used; i++) {
    buf[i / 8] = buf[i / 8] | (0x1 << (i % 8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char *)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while (n > 0) {
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if (fbn < NDIRECT) {
      if (xint(din.addrs[fbn]) == 0) {
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if (xint(din.addrs[NDIRECT]) == 0) {
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char *)indirect);
      if (indirect[fbn - NDIRECT] == 0) {
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char *)indirect);
      }
      x = xint(indirect[fbn - NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

void
die(const char *s)
{
  perror(s);
  exit(1);
}
