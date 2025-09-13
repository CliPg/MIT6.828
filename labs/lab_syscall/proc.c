/**
 * For sysinfo, to collect the number of processes
 */

uint64
nproc(void)
{
  int n = 0;
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED) {
      n++;
    }
    release(&p->lock);
  }
  return n;
}