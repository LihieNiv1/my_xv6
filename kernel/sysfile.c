//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd] == 0)
    {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
  {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip))
  {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR)
  {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
  {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR)
  { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR)
  {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE)
  {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0)
    {
      end_op();
      return -1;
    }
  }
  else
  {
    if ((ip = namei(path)) == 0)
    {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY)
    {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV))
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0)
  {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE)
  {
    f->type = FD_DEVICE;
    f->major = ip->major;
  }
  else
  {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE)
  {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0)
  {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++)
  {
    if (i >= NELEM(argv))
    {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
    {
      goto bad;
    }
    if (uarg == 0)
    {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0)
  {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0)
  {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

int check_if_vma_free(uint64 addr, uint32 len)
{
  struct proc *p = myproc();
  struct vma *vma_list = p->vma_list;
  len = PGROUNDUP(len);
  if (addr + len > MAXVA)
    return -1;
  for (int i = 0; i < 16; i++)
  {
    if (vma_list[i].vm_file == 0)
      continue;
    if ((vma_list[i].start < addr + len) && (vma_list[i].end > addr))
    {
      return -1;
    }
  }
  return 0;
}

uint64 find_free_range(uint32 len)
{

  uint32 num_pages = PGROUNDUP(len);
  struct proc *p = myproc();
  uint64 cur_addr = PGROUNDUP(p->sz);
  uint64 start_addr = cur_addr;
  pagetable_t pagetable = p->pagetable;
  while (cur_addr < MAXVA)
  {
    if (cur_addr == start_addr)
    {
      if (check_if_vma_free(start_addr, len) == -1) // Intersect with vma for the next len.
      {
        cur_addr += PGSIZE;
        start_addr = cur_addr;
        continue;
      }
    }
    if (walkaddr(pagetable, cur_addr)) // already mapped
    {
      cur_addr += PGSIZE;    // skip it
      start_addr = cur_addr; // start searching from scratch
      continue;
    }
    cur_addr += PGSIZE;
    if (cur_addr - start_addr >= num_pages)
      return start_addr;
  }
  return -1;
}

// user call - void *mmap(void *addr, uint32 length, int prot, int flags, int fd, uint32 offset);
uint64
sys_mmap(void)
{
  struct proc *p = myproc();
  uint64 u_addr;
  uint32 length;
  int temp;
  int prot;
  int flags;
  int fd;
  struct file *f;
  uint32 offset;
  argaddr(0, &u_addr);
  argint(1, &temp);
  length = (uint32)temp;
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &f);
  argint(5, &temp);
  offset = (uint32)temp;
  if (u_addr != 0)
  {
    return -1;
  }
  if ((prot & (PROT_READ | PROT_WRITE)) == 0)
  {
    return -1;
  }
  if ((flags != MAP_SHARED) && (flags != MAP_PRIVATE))
  {
    return -1;
  }
  if ((f->readable == 0) && (prot & PROT_READ))
  {
    return -1;
  }

  if ((f->writable == 0) && (prot & PROT_WRITE) && (flags == MAP_SHARED))
  {
    return -1;
  }
  uint64 start_addr = find_free_range(length);
  if (start_addr == -1)
    return -1;
  struct vma *vma = 0;
  for (int index = 0; index < 16; index++)
  {
    if (p->vma_list[index].vm_file == 0)
    {
      vma = p->vma_list + index;
      break;
    }
  }
  if (vma == 0) // No more vma's
  {
    return -1;
  }
  vma->start = start_addr;
  vma->end = start_addr + length;
  vma->prot = prot;
  vma->flags = flags;
  vma->f_offset = offset;
  vma->vm_file = f;
  filedup(f); // Increase file ref count
  return start_addr;
}

int munmap_kern(uint64 addr, uint32 length)
{
  struct proc *p = myproc();
  if (addr != PGROUNDDOWN(addr))
    return -1;
  if (length % PGSIZE != 0)
    return -1;
  struct vma *vma = 0;
  for (int i = 0; i < 16; i++)
  {
    if (p->vma_list[i].start <= addr && p->vma_list[i].end >= addr + length)
    {
      vma = p->vma_list + i;
      break;
    }
  }
  if (vma == 0)
    return 0; // apparently this is the expected behaviour
  if (addr != vma->start && addr + length != vma->end)
    return -1;

  if (walkaddr(p->pagetable, addr))
  {
    pte_t *pte = walk(p->pagetable, addr, 0);
    if (vma->flags == MAP_SHARED && (PTE_FLAGS(*pte) & PTE_D))
    {
      begin_op();
      ilock(vma->vm_file->ip);
      int r;
      r = writei(vma->vm_file->ip, 1, addr, vma->f_offset + addr - vma->start, length);
      iunlock(vma->vm_file->ip);
      end_op();
      if (r < length)
      {
        return -1;
      }
    }

    uvmunmap(p->pagetable, addr, PGROUNDUP(length) / PGSIZE, 1);
  }
  if (vma->start == addr)
    vma->start = addr + length;
  if (vma->end == addr + length)
    vma->end = addr;
  if (vma->end <= vma->start) // removed entire mapping
  {
    fileclose(vma->vm_file);
    vma->vm_file = 0;
  }
  return 0;
}

// user call - int munmap(void *addr, uint32 length);
uint64 sys_munmap(void)
{
  uint64 addr;
  int temp;
  argaddr(0, &addr);
  argint(1, &temp);
  uint32 length = (uint32)temp;
  return (uint64)munmap_kern(addr, length);
}

int mmap_handler(uint64 va)
{
  struct proc *p = myproc();
  struct vma *vma = 0;
  for (int i = 0; i < 16; i++)
  {
    if (p->vma_list[i].start <= va && p->vma_list[i].end > va)
    {
      vma = p->vma_list + i;
      break;
    }
  }
  if (vma == 0) // shouldnt happen but just in case
    return -1;
  if (walkaddr(p->pagetable, va))
  {
    return -1;
  }
  void *pa = kalloc();
  if (pa == 0)
    return -1;
  int perm = PTE_U;
  if (vma->prot & PROT_READ)
    perm += PTE_R;
  if (vma->prot & PROT_WRITE)
    perm += PTE_W;
  if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa, perm) != 0)
  {
    kfree(pa);
    return -1;
  }
  memset(pa, 0, PGSIZE);
  ilock(vma->vm_file->ip);
  int r;
  r = readi(vma->vm_file->ip, 0, (uint64)pa, vma->f_offset + PGROUNDDOWN(va) - vma->start, PGSIZE);
  iunlock(vma->vm_file->ip);
  if (r <= 0)
  {
    uvmunmap(p->pagetable, PGROUNDDOWN(va), 1, 1);
    return -1;
  }
  return 0;
}