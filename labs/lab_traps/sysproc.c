uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->fn_ret = 1;
  memmove(p->trapframe, p->trapframe_backup, sizeof(struct trapframe));
  release(&p->lock);
  return p->trapframe->a0;
}

uint64
sys_sigalarm(void)
{
  int n;
  uint64 fn;
  argint(0, &n);
  argaddr(1, &fn);
  struct proc *p = myproc();
  acquire(&p->lock);
  p->expired_ticks = 0;
  p->alarm_ticks = n;
  p->fn = fn;
  release(&p->lock);
  return 0;
}