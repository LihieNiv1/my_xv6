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

#define NBINS 19
#define BHASH(x) (x % NBINS)
struct
{
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

struct bcache_bin
{
  struct spinlock lock;
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
};

struct bcache_bin bcache_table[NBINS];

void binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NBINS; i++)
  {
    initlock(&bcache_table[i].lock, "bcache.bin");
    bcache_table[i].head.prev = &(bcache_table[i].head);
    bcache_table[i].head.next = &(bcache_table[i].head);
  }
  // Create linked list of buffers
  // currently use this only for all initial blocks to be in one place.
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;

  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    initsleeplock(&b->lock, "buffer");
    /*b->next = bcache_table[i].head.next;
    b->prev = &(bcache_table[i].head);
    bcache_table[i].head.next->prev = b;
    bcache_table[i].head.next = b;*/
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

uint count = 0;

void bfree_unused(int cur_bin)
{
  /*for (int i = 0; i < NBINS; i++)
  {
    if (i != cur_bin)
      acquire(&bcache_table[i].lock);
  }*/
  struct buf *b;
  struct bcache_bin *b_bin;
  // int flag = 0;
  for (int i = 0; i < NBINS; i++)
  {
    if (i == cur_bin)
      continue;
    b_bin = bcache_table + i;
    acquire(&(b_bin->lock));
    for (b = b_bin->head.prev; b != &(b_bin->head); b = b->prev)
    {
      if (b->refcnt == 0)
      {
        // get out of current bin
        b->prev->next = b->next;
        b->next->prev = b->prev;
        // add to bcache
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
        break;
        // flag = 1;
      }
    }
    release(&(b_bin->lock));
  }
  /*for (int i = 0; i < NBINS; i++)
  {
    if (i != cur_bin)
      release(&bcache_table[i].lock);
  }*/
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  int hash = BHASH(blockno);
  struct bcache_bin *b_bin = bcache_table + hash;
  acquire(&b_bin->lock);
  // Is the block already cached?
  for (b = b_bin->head.next; b != &b_bin->head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&b_bin->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Try and find free entry in this bin
  for (b = b_bin->head.prev; b != &b_bin->head; b = b->prev)
  {
    if (b->refcnt == 0)
    {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&b_bin->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  acquire(&bcache.lock);
  count++;
  if (bcache.head.next == &bcache.head)
    bfree_unused(hash);
  if (bcache.head.next == &bcache.head) // no free buffers in this bin or any other bin or in bcache list
    panic("bget: no buffers");

  b = bcache.head.next;
  // Remove bcache.head.next from b_bin
  bcache.head.next = bcache.head.next->next;
  bcache.head.next->prev = &bcache.head;
  // Can add bcache.head.next to b_bin:
  b->prev = &b_bin->head;
  b->next = b_bin->head.next;
  b_bin->head.next->prev = b;
  b_bin->head.next = b;
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  count--;
  release(&bcache.lock);
  release(&b_bin->lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int hash = BHASH(b->blockno);
  struct bcache_bin *b_bin = bcache_table + hash;
  acquire(&b_bin->lock);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = b_bin->head.next;
    b->prev = &b_bin->head;
    b_bin->head.next->prev = b;
    b_bin->head.next = b;
  }
  release(&b_bin->lock);
}

void bpin(struct buf *b)
{
  acquire(&bcache_table[BHASH(b->blockno)].lock);
  b->refcnt++;
  release(&bcache_table[BHASH(b->blockno)].lock);
}

void bunpin(struct buf *b)
{
  acquire(&bcache_table[BHASH(b->blockno)].lock);
  b->refcnt--;
  release(&bcache_table[BHASH(b->blockno)].lock);
}
