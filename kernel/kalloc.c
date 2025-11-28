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

// Per-CPU free lists to reduce lock contention (LAB5 part 1).
// Each CPU has its own lock and freelist. When a CPU's freelist is empty
// kalloc() will attempt to steal one page from another CPU's freelist.
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

static char kmem_locknames[NCPU][8]; // e.g. "kmem0", "kmem1" ... for stats aggregation.

void
kinit()
{
  // Initialize per-CPU locks with distinct names starting with "kmem".
  for(int i = 0; i < NCPU; i++) {
    snprintf(kmem_locknames[i], sizeof(kmem_locknames[i]), "kmem%d", i);
    initlock(&kmem[i].lock, kmem_locknames[i]);
    kmem[i].freelist = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  // Distribute pages round-robin across CPU freelists to pre-populate them.
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  int cpu_index = 0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    struct run *r = (struct run*)p;
    // Basic sanity fill; identical to kfree's memset but avoid recursive lock usage.
    memset(p, 1, PGSIZE);
    acquire(&kmem[cpu_index].lock);
    r->next = kmem[cpu_index].freelist;
    kmem[cpu_index].freelist = r;
    release(&kmem[cpu_index].lock);
    cpu_index = (cpu_index + 1) % NCPU;
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;
  int id = cpuid();

  // First try local freelist.
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r) {
    kmem[id].freelist = r->next;
    release(&kmem[id].lock);
  } else {
    release(&kmem[id].lock);
    // Steal from another CPU's freelist.
    for(int i = 0; i < NCPU; i++) {
      if(i == id) continue;
      acquire(&kmem[i].lock);
      if(kmem[i].freelist) {
        r = kmem[i].freelist;
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
