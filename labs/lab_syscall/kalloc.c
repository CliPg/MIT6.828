/**
 * For sysinfo, to collect the amount of free memory
 */

uint64
kfreemem()
{
  int n = 0;
  struct run *r;
  
  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r) {
    n++;
    r = r->next;
  }
  release(&kmem.lock);

  return n*PGSIZE;
}