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

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
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

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
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

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
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

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
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

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
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

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
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

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
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

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
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
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
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
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
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
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
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

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
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

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// 添加 sys_mmap
// Implement mmap: find an unused region in the process's address space 
//  in which to map the file, and add a VMA to the process's table of 
//  mapped regions. The VMA should contain a pointer to a struct file 
//  for the file being mapped; mmap should increase the file's reference 
//  count so that the structure doesn't disappear when the file is closed 
//  (hint: see filedup).
// 懒分配（lazy allocation）文件映射的系统调用接口，是实现 mmap() 的核心之一。它的作用是：
//  将一个文件“虚拟”地映射到用户进程地址空间中的一段地址上，
//  但并不立即分配物理内存或读取文件内容，
//  而是等到程序实际访问这些地址时触发 page fault，再分配物理内存并读入数据。
uint64 
sys_mmap(void){
  uint64 addr;
  int length, prot, flags, fd, offset;

  argaddr(0, &addr); // 会忽略，始终由内核分配
                     // You can assume that addr will always be zero, 
                     //   meaning that the kernel should decide the virtual 
                     //   address at which to map the file. mmap returns that address, 
                     //   or 0xffffffffffffffff if it fails. 
  argint(1, &length); // the number of bytes to map
  argint(2, &prot); // indicates whether the memory should be mapped readable, writeable, and/or executable
  argint(3, &flags); // either MAP_SHARED, meaning that modifications to the mapped memory 
                     //  should be written back to the file, or MAP_PRIVATE, meaning that they should not. 
  argint(4, &fd); // the open file descriptor of the file to map
  argint(5, &offset); // You can assume offset is zero 
                      // (it's the starting point in the file at which to map)

  if(length <= 0)
    return -1;

  struct file *f = myproc()->ofile[fd];   // open the file
  if(f == 0)
    return -1;
  
  // PROT_WRITE + MAP_SHARED 映射，但文件却只读打开 —— 不允许
  if ((prot & PROT_WRITE) && (flags & MAP_SHARED) && !f->writable) {
    return -1;
  }
  
  struct proc *p = myproc();
  for(int i = 0; i < MAX_VMA; i++){
    if(!p->vmas[i].used){
      uint64 va = PGROUNDUP(p->sz);  // 从当前 sz 之上分配新地址空间
      p->vmas[i].used = 1;
      p->vmas[i].addr = va;
      p->vmas[i].length = length;
      p->vmas[i].prot = prot;
      p->vmas[i].flags = flags;
      p->vmas[i].f = filedup(f);  // 通过 filedup(f) 保持文件结构的生命周期, 
                                  // 防止用户关闭文件描述符后 struct file* 被回收
      p->vmas[i].file_offset = offset;

      p->sz = va + length;         // 扩展 sz
      return va;
    }
  }
  return -1; // no free vma
}

// 添加sys_munmap
// Implement munmap: find the VMA for the address range and unmap the 
//  specified pages (hint: use uvmunmap). If munmap removes all pages 
//  of a previous mmap, it should decrement the reference count of the 
//  corresponding struct file. If an unmapped page has been modified and 
//  the file is mapped MAP_SHARED, write the page back to the file. 
//  Look at filewrite for inspiration. 
// Ideally your implementation would only write back MAP_SHARED pages 
//  that the program actually modified. The dirty bit (D) in the RISC-V 
//  PTE indicates whether a page has been written. However, mmaptest does 
//  not check that non-dirty pages are not written back; thus you can get 
//  away with writing pages back without looking at D bits.
uint64 
sys_munmap(void){
  uint64 addr;
  int length;

  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;
  
  if (addr % PGSIZE != 0 || length % PGSIZE != 0)
    return -1;

  struct proc *p = myproc();
  struct vma *v = 0;

  // 找到与 addr 匹配的 VMA
  for (int i = 0; i < MAX_VMA; i++) {
    if (!p->vmas[i].used)
      continue;
    if (addr >= p->vmas[i].addr &&
        addr + length <= p->vmas[i].addr + p->vmas[i].length) {
      v = &p->vmas[i];
      break;
    }
  }

  if (v == 0)
    return -1; // 未找到匹配的映射

  // 写回已修改页（如果是 MAP_SHARED）
  if ((v->flags & MAP_SHARED) && (v->prot & PROT_WRITE) && (v->f->writable)) {
    for (uint64 a = addr; a < addr + length; a += PGSIZE) {
      pte_t *pte = walk(p->pagetable, a, 0); // walk() 获取虚拟地址 a 对应的页表项（PTE）。
                                             // 第三个参数为 0 表示不创建新的页表项
      // if (pte && (*pte & PTE_V) && (*pte & PTE_D)) {
      // xv6 2020 默认不支持 PTE_D，这段代码只在添加了 PTE_D 支持时才有效
      if (pte && (*pte & PTE_V)) {
        uint64 pa = PTE2PA(*pte); // 通过 PTE 解码获取物理地址
        begin_op();
        ilock(v->f->ip); // 锁 inode
        // 把这段页的内容（pa）写入文件：
        //   偏移 = a - v->addr + v->file_offset
        //   a - v->addr 是映射区域内偏移
        //   加上文件起始偏移
        writei(v->f->ip, 0, (uint64)pa, a - v->addr + v->file_offset, PGSIZE);
        iunlock(v->f->ip);
        end_op();
      }
    }
  }

  // 必须先判断该范围内的虚拟地址是否真的映射过——只调用 uvmunmap() 删除已映射的页。
  int mapped = 0;
  for (uint64 a = addr; a < addr + length; a += PGSIZE) {
    pte_t *pte = walk(p->pagetable, a, 0);
    if (pte && (*pte & PTE_V)) {
      mapped = 1;
      break;
    }
  }
  if (mapped)
    uvmunmap(p->pagetable, addr, length / PGSIZE, 0);  // 注意 0 表示不强制释放

  // An munmap call might cover only a portion of an mmap-ed region, 
  //  but you can assume that it will either unmap at the start, 
  //  or at the end, or the whole region (but not punch a hole in the middle of a region).
  if (addr == v->addr && length == v->length) {
    // 如果 addr 和 length 正好覆盖了整个 VMA：
    //   1. 就完全清除它；
    //   2. 调用 fileclose()，因为我们在 mmap() 中调用了 filedup() 增加了引用计数，现在要还原；
    //   3. 设置 v->used = 0 表示此 VMA 槽位可重用。
    fileclose(v->f);
    v->used = 0;
  } else if (addr == v->addr) {
    // 前端裁剪
    // 如果 munmap() 解除的是 VMA 的起始一段，我们：
    //   1. 增加 VMA 的起始地址；
    //   2. 减少 VMA 的长度；
    //   3. 同时要更新对应文件映射的 file_offset，因为现在偏移过去了。
    v->addr += length;
    v->length -= length;
    v->file_offset += length;
  } else if (addr + length == v->addr + v->length) {
    // 后端裁剪
    // 如果解除的是 VMA 的末尾区域：
    //   1. 只需减少 length 即可；
    //   2. addr 不变，因为起始地址没变；
    //   3. 不需要改 file_offset，因为文件映射的起点没变。
    v->length -= length;
  } else {
    // xv6 不支持中间打洞
    return -1;
  }

  return 0;
}