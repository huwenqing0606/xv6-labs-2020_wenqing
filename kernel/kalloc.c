// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 用于管理物理页面引用计数
struct {
  struct spinlock lock;       // 对 count[] 的访问加锁
  int count[PHYSTOP / PGSIZE]; // 每一页的引用计数
} refcnt;

// Ensure that each physical page is freed when the last PTE reference to it goes away 
//  -- but not before. A good way to do this is to keep, for each physical page, a 
//  "reference count" of the number of user page tables that refer to that page. Set 
//  a page's reference count to one when kalloc() allocates it. Increment a page's 
//  reference count when fork causes a child to share the page, and decrement a page's 
//  count each time any process drops the page from its page table. 
//  kfree() should only place a page back on the free list if its reference count is zero. 
//  It's OK to to keep these counts in a fixed-size array of integers. 
//  You'll have to work out a scheme for how to index the array and how to choose its size. 
//  For example, you could index the array with the page's physical address divided by 4096, 
//  and give the array a number of elements equal to highest physical address of any page 
//  placed on the free list by kinit() in kalloc.c.
// free page 的时候需要考虑该页面是不是被多个进程共享，
//  这里提出来了一种引用计数的方法保证页面的安全销毁，设置一个bitmap用于映射，
//  将物理地址除以一个页面大小的商作为bitmap的大小，然后在kalloc或者kfree时分别对引用计数做处理
// 初始化所有page，设置引用计数为0
void
rcinit()
{
  initlock(&refcnt.lock, "refcnt");
  acquire(&kmem.lock);
  for (int i = 0; i < PGROUNDUP(PHYSTOP) / PGSIZE; i++)
    refcnt.count[i] = 1;
  release(&kmem.lock);
}

void
kinit()
{
  rcinit();
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// kfree判断是否 ref_cnt 引用计数为 0 ，决定是否free page
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // ref_cnt 引用数减 1
  decrease_rc(pa);
  // 判断 ref_cnt 引用数减1后是否>0
  if (get_rc(pa) > 0)
    return;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 申请时添加 ref_cnt 计数
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    increase_rc((void*) r); // 添加 ref_cnt 计数
  }
  return (void*)r;
}

// 对 ref_cnt 的系列操作
// rc + 1
void increase_rc(void* pa) {
  acquire(&refcnt.lock);
  refcnt.count[(uint64)pa / PGSIZE]++;
  release(&refcnt.lock);
}
// rc - 1
void decrease_rc(void* pa) {
  acquire(&refcnt.lock);
  refcnt.count[(uint64)pa / PGSIZE]--;
  release(&refcnt.lock);
}
// rc
int get_rc(void* pa) {
  acquire(&refcnt.lock);
  int n = refcnt.count[(uint64)pa / PGSIZE];
  release(&refcnt.lock);
  return n;
}