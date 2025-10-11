// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#define MAX_REFCOUNT_PAGES  ((PHYSTOP - KERNBASE) / PGSIZE)

struct spinlock ref_lock;
int refcount[MAX_REFCOUNT_PAGES];  // 全局引用计数数组，默认初始化为0

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

uint64
get_ref_id(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
add_ref(void* pa)
{
  acquire(&ref_lock);
  refcount[get_ref_id((uint64)pa)]++;
  release(&ref_lock);
}

void
dec_ref(void* pa)
{
  acquire(&ref_lock);
  refcount[get_ref_id((uint64)pa)]--;
  release(&ref_lock); 
}

void*
cow_pa(void* pa)
{
  acquire(&ref_lock);
  if(refcount[get_ref_id((uint64)pa)] <= 1){
    release(&ref_lock);
    return pa;
  }
  char* new = kalloc();
  if(new == 0){
    release(&ref_lock);
    return 0;
  }
  memmove((void*)new, pa, PGSIZE);
  refcount[get_ref_id((uint64)pa)]--;
  release(&ref_lock);
  return (void*)new;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_lock, "refcount");
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

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref_lock);
  refcount[get_ref_id((uint64)pa)]--;
  if(refcount[get_ref_id((uint64)pa)] <= 0){
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&ref_lock);

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  if(r){   
    memset((char*)r, 5, PGSIZE); 
    refcount[get_ref_id((uint64)r)] = 1;  // 新页引用计数初始化为1
  }
  
  return (void*)r;
}
