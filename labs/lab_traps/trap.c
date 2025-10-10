void
usertrap(void)
{
  int which_dev = 0;

  // SSTATUS——SPP标识trap来源（0=用户态，1=内核态）
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 把stvec（trap向量基址寄存器）设置为内核模式下的中断向量kernelvec
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 保存用户态的pc
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    acquire(&p->lock);
    p->expired_ticks++;
    if (p->alarm_ticks != 0 && p->expired_ticks >= p->alarm_ticks && p->fn_ret) {
      memmove(p->trapframe_backup, p->trapframe, sizeof(struct trapframe));
      p->expired_ticks = 0;
      p->trapframe->epc = p->fn;
      p->fn_ret = 0;
    }
    release(&p->lock);
    yield();
  }
  usertrapret();
}