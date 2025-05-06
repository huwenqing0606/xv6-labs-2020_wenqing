// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 用质数13作为hash table大小，将buf分段映射到不同的hash桶里
#define NBUCKETS 13

// 实现hash函数
static int hash(uint blockno) {
  return blockno % NBUCKETS;
}

struct {
  // 为每个桶准备一个锁
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // 每个桶有一个head
  struct buf head[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;

  // 初始化 buffer_cache，针对每个桶进行遍历
  for (int i=0; i<NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    acquire(&bcache.lock[i]);
    // Create linked list of buffers
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
    // 每个桶大小 NBUF/NBUCKETS 
    for(b = &bcache.buf[i*(NBUF/NBUCKETS)]; b < &bcache.buf[(i+1)*(NBUF/NBUCKETS)]; b++){
      b->next = bcache.head[i].next;
      b->prev = &bcache.head[i];
      initsleeplock(&b->lock, "buffer");
      bcache.head[i].next->prev = b;
      bcache.head[i].next = b;
    }
    release(&bcache.lock[i]);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 针对特定的桶
  int id = hash(blockno);
  acquire(&bcache.lock[id]);

  // 如果找到相应缓冲区的话，就简单返回
  // Is the block already cached?
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // 如果没找到相应缓冲区，则试图找一个未被使用的 LRU buffer
  // 1. 先在当前的桶里面找有无空余的buffer
  for(b = bcache.head[id].prev; b != &bcache.head[id]; b = b->prev){
    if(b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[id]);
  // 2. 从另一个桶找一个未被使用的buffer
  for(int j = (id + 1) % NBUCKETS; j != id; j = (j + 1) % NBUCKETS){
    acquire(&bcache.lock[j]);
    for(b = bcache.head[j].prev; b != &bcache.head[j]; b = b->prev){
      if(b->refcnt == 0){
        // Remove from old bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        // Set up new block info
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // Move to new bucket
        release(&bcache.lock[j]);
        acquire(&bcache.lock[id]);
        b->next = bcache.head[id].next;
        b->prev = &bcache.head[id];
        bcache.head[id].next->prev = b;
        bcache.head[id].next = b;
        release(&bcache.lock[id]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[j]);
  }

  // 如果别的hash桶里没有的话就报错
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = hash(b->blockno);  
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[id].next;
    b->prev = &bcache.head[id];
    bcache.head[id].next->prev = b;
    bcache.head[id].next = b;
  }
  
  release(&bcache.lock[id]);
}

void
bpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


