/**
 * System call implementations.
 */

uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->tracemask = mask;
  return 0;
}

uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  uint64 uaddr;

  argaddr(0, &uaddr);

  info.freemem = kfreemem();
  info.nproc = nproc();

  struct proc *p = myproc();
  if (copyout(p->pagetable, uaddr, (char *)&info, sizeof(info)) < 0) {
    return -1;
  }
  return 0;
  
}