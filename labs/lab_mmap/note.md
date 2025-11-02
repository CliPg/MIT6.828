vma 是操作系统中一个非常重要的概念，全称是 Virtual Memory Area（虚拟内存区域）。它是内核用于管理进程虚拟地址空间的一种数据结构。

在 Linux 内核中，每个进程都有多个虚拟地址空间（Virtual Address Space）。
而这个空间并不是一整块连续的区域，而是被分割成若干段，每一段都有自己的用途，例如：
- 程序代码段（text segment）
- 数据段（data segment）
- 堆（heap）
- 栈（stack）
- 内存映射文件区域（memory-mapped file area）

这些“段”在内核中就用 vma 结构体（struct vm_area_struct） 来描述。

在kerne/proc.h 定义vma
```
#define VMASIZE 16
struct vma {
  int used; // 是否使用
  uint64 addr; // 映射起始虚拟地址
  int length; // 映射长度
  int prot; // 保护属性（读/写/执行）
  int flags; // 是否共享映射
  int fd; // 文件描述符
  int offset; // 文件偏移
  struct file *file; // 对应文件结构体
};
```

首先实现mmap函数。
```
// 将文件映射到虚拟内存
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file *file;
  struct proc *p = myproc();
  argaddr(0, &addr);
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &file);
  argint(5, &offset);

  if (!file->writable && (prot & PROT_WRITE) && flags == MAP_SHARED) {
    return -1;
  }
  length = PGROUNDUP(length);
  if (p->sz > MAXVA - length) {
    return -1;
  }
  for (int i = 0; i < VMASIZE; i++) {
    // 查找空闲的vma槽位
    if (p->vma[i].used == 0) {
      p->vma[i].used = 1;
      p->vma[i].addr = p->sz;
      p->vma[i].length = length;
      p->vma[i].prot = prot;
      p->vma[i].flags = flags;
      p->vma[i].fd = fd;
      p->vma[i].file = file;
      p->vma[i].offset = offset;
      filedup(file); // 文件引用计数+1，防止文件提前关闭
      p->sz += length;
      return p->vma[i].addr;
    }
  }
  return -1;
}
```
mmap将进程的空闲vma写入需要映射的文件的文件描述符和其他信息。

然后实现trap的中断处理。找到空闲vma，然后分配一块内存，将文件写入内存中。
```
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
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
  } else if (r_scause() == 13 || r_scause() == 15) {
    uint64 va = r_stval();
    if (va >= p->sz || va > MAXVA || PGROUNDUP(va) == PGROUNDDOWN(p->trapframe->sp)) {
      p->killed = 1;
    } else {
      struct vma *vma = 0;
      for (int i = 0; i < VMASIZE; i++) {
        if (p->vma[i].used == 1 && va >= p->vma[i].addr && va < p->vma[i].addr + p->vma[i].length) {
          vma = &p->vma[i];
          break;
        }
      }
      if (vma) {
        va = PGROUNDDOWN(va);
        uint64 offset = va - vma->addr;
        uint64 mem = (uint64)kalloc();
        if (mem == 0) {
          p->killed = 1;
        } else {
          memset((void*)mem, 0, PGSIZE);
          ilock(vma->file->ip);
          readi(vma->file->ip, 0, mem, offset, PGSIZE);
          iunlock(vma->file->ip);
          int flag = PTE_U;
          if (vma->prot & PROT_READ) flag |= PTE_R;
          if (vma->prot & PROT_WRITE) flag |= PTE_W;
          if (vma->prot & PROT_EXEC) flag |= PTE_X;
          if (mappages(p->pagetable, va, PGSIZE, mem, flag) != 0) {
            kfree((void*)mem);
            p->killed = 1;
          }          
        }
      }
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
```