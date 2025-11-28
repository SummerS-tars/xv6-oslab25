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

#define NBUCKETS 13

// 分桶的 buffer cache，每个桶有独立的自旋锁与双向链表头（作为哨兵）。
struct bcache_bucket {
  struct spinlock lock;   // "bcacheX" for stats aggregation
  struct buf head;        // circular doubly-linked list; head.next = MRU, head.prev = LRU
};

struct {
  struct buf buf[NBUF];
  struct bcache_bucket buckets[NBUCKETS];
} bcache;

static inline int
bucket_idx(uint dev, uint blockno)
{
  return (dev ^ blockno) % NBUCKETS;
}

void binit(void)
{
  // 初始化每个桶的锁与双向链表头
  for (int i = 0; i < NBUCKETS; i++) {
    char name[16];
    snprintf(name, sizeof(name), "bcache%d", i);
    initlock(&bcache.buckets[i].lock, name);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // 初始化所有 buffer，并以轮转方式分布到各桶（初始均为空闲，refcnt=0）
  int bi = 0;
  for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++, bi++){
    initsleeplock(&b->lock, "buffer");
    b->valid = 0;
    b->dev = 0;
    b->blockno = 0;
    b->refcnt = 0;

    int idx = bi % NBUCKETS;
    struct bcache_bucket *buck = &bcache.buckets[idx];
    acquire(&buck->lock);
    // 插到 MRU 位置（head.next）
    b->next = buck->head.next;
    b->prev = &buck->head;
    buck->head.next->prev = b;
    buck->head.next = b;
    release(&buck->lock);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf* bget(uint dev, uint blockno) {
  struct buf *b;
  int home = bucket_idx(dev, blockno);
  struct bcache_bucket *hb = &bcache.buckets[home];

  // 1) 先在 home 桶查找
  acquire(&hb->lock);
  for (b = hb->head.next; b != &hb->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&hb->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 2) 不在缓存：尝试在 home 桶回收一个空闲 buf（LRU 方向优先）
  for (b = hb->head.prev; b != &hb->head; b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&hb->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&hb->lock);

  // 3) 无可用：从其他桶“窃取”一个空闲 buf
  for (int i = 0; i < NBUCKETS; i++) {
    if (i == home) continue;
    struct bcache_bucket *vb = &bcache.buckets[i];
    acquire(&vb->lock);
    struct buf *v;
    for (v = vb->head.prev; v != &vb->head; v = v->prev) {
      if (v->refcnt == 0) {
        // 从 victim 桶移除，并“预留”该 buf：设置 refcnt=1 和新的标识
        v->next->prev = v->prev;
        v->prev->next = v->next;
        v->dev = dev;
        v->blockno = blockno;
        v->valid = 0;
        v->refcnt = 1;
        release(&vb->lock);

        // 将其插入 home 桶的 MRU 位置
        acquire(&hb->lock);
        v->next = hb->head.next;
        v->prev = &hb->head;
        hb->head.next->prev = v;
        hb->head.next = v;
        release(&hb->lock);

        acquiresleep(&v->lock);
        return v;
      }
    }
    release(&vb->lock);
  }
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

  int idx = bucket_idx(b->dev, b->blockno);
  struct bcache_bucket *buck = &bcache.buckets[idx];
  acquire(&buck->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 移动到 MRU 位置
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = buck->head.next;
    b->prev = &buck->head;
    buck->head.next->prev = b;
    buck->head.next = b;
  }
  release(&buck->lock);
}

void
bpin(struct buf *b) {
  int idx = bucket_idx(b->dev, b->blockno);
  acquire(&bcache.buckets[idx].lock);
  b->refcnt++;
  release(&bcache.buckets[idx].lock);
}

void
bunpin(struct buf *b) {
  int idx = bucket_idx(b->dev, b->blockno);
  acquire(&bcache.buckets[idx].lock);
  b->refcnt--;
  release(&bcache.buckets[idx].lock);
}


