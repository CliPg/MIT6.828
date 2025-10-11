问题描述

在 xv6 中，fork() 系统调用会将父进程的整个用户空间内存复制到子进程中。
如果父进程占用的内存很大，这个复制过程就会非常耗时。更糟的是，这项工作往往是浪费的：fork() 通常会紧接着在子进程中调用 exec()，而 exec() 会丢弃子进程原有的内存拷贝，通常甚至不会使用其中的大部分数据。
另一方面，如果父进程和子进程确实都要使用同一个被拷贝的页面，并且其中之一或双方要对它进行写操作，那么这种拷贝才是必须的。

解决方案

你的目标是实现 写时复制（Copy-On-Write, COW） 的 fork()，通过推迟物理内存页面的分配与复制，直到确实需要时才执行，从而提高效率。

在 COW fork() 中，系统仅为子进程创建一个页表（pagetable），并让子进程的页表项（PTE）指向父进程相同的物理页面。
接着，COW fork() 会将父进程和子进程中所有用户空间页面的页表项都标记为 只读（read-only）。
当任意一个进程试图向这些 COW 页面写入时，CPU 就会触发一个 页面错误（page fault）。
内核的 页面错误处理程序（page-fault handler） 会检测到这是一个写时复制的情况，然后执行以下步骤：
	1.	为触发错误的进程分配一页新的物理内存；
	2.	将原页面的内容复制到新的页面中；
	3.	修改该进程对应的页表项，让它指向新页面，并将页表项标记为可写（writeable）。

当页面错误处理完成并返回后，用户进程就可以顺利地在自己独立的那份页面上进行写操作了。

额外注意：

COW fork() 使得用户内存所使用的物理页的释放过程变得更加复杂。
同一个物理页面可能会被多个进程的页表引用，只有在最后一个引用消失时，这个物理页才能被真正释放。
在像 xv6 这样的简单内核中，这种引用计数的管理还比较容易实现，但在生产级内核中，这种机制可能会非常复杂且容易出错。例如，Linux 就曾在这一机制上出现过问题，详见文章 “Patching until the COWs come home”。

⚙️ 实现步骤（推荐方案）

1️⃣ 修改 uvmcopy()

原版 uvmcopy() 会为子进程分配新物理页并复制父进程内容。
在 COW 版本中，改成以下逻辑：
	•	不再为子进程分配新页；
	•	让子进程的页表项（PTE）直接指向父进程相同的物理页；
	•	对于原本可写（PTE_W）的页面，同时清除父进程与子进程的 PTE_W 位；
	•	可选：为该页表项添加一个标志位（例如使用 RSW 位），标识此页是 COW 页。

⸻

2️⃣ 修改 usertrap()

当用户进程发生 页面错误（page fault） 时，usertrap() 会被调用。

实现以下逻辑：
	•	检测页面错误是否是写操作导致的；
	•	如果是写操作且该页是 COW 页：
	1.	调用 kalloc() 分配一页新的物理内存；
	2.	将原页面内容复制过去；
	3.	更新页表项，使其指向新页，并重新设置 PTE_W；
	•	如果该页原本是只读页（例如程序代码段），不允许写入，应直接 kill 掉进程；
	•	如果没有可用的物理页（kalloc() 返回 0），也应 kill 掉进程。

⸻

3️⃣ 为每个物理页维护引用计数（Reference Count）

COW 机制意味着多个进程可能共享同一个物理页。
因此，在释放内存时，只有当该页最后一个引用被释放，才能真正释放它。

实现方法：
	•	在 kalloc.c 中添加一个全局数组（例如 int refcount[]），用于记录每个物理页的引用计数；
	•	索引规则： 可用物理页的物理地址 / 4096（页大小）作为索引；
	•	数组大小： 可等于 kinit() 初始化时可用物理内存的最大页数；
	•	在 kalloc() 分配新页时：refcount[page_index] = 1
	•	在 fork 的 uvmcopy() 共享页时：refcount[page_index]++
	•	在 kfree() 释放页时：refcount[page_index]--
	•	仅当引用计数降为 0 时，才真正将页放回空闲链表。

⸻

4️⃣ 修改 copyout()

copyout() 负责从内核空间拷贝数据到用户空间。
若目标页为 COW 页，执行与页面错误时相同的“分配新页 + 复制内容 + 更新页表”逻辑。

⸻

🧠 一些提示
	•	你可以利用 RISC-V 页表中的 RSW（reserved for software）位 来标记哪些页是 COW 页；
	•	cowtest 主要测试内存共享与页面复制；
	•	usertests -q 会进一步测试各种边界情况（如 exec、mmap 等），请确保它们也通过；
	•	页表标志宏可以在 kernel/riscv.h 文件末尾找到；
	•	当发生 COW 页面错误但系统内存不足（kalloc() 返回空）时，应 kill 掉对应进程。


1. 
```
kernel/vm.c

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    //对于原本可写（PTE_W）的页面，同时清除父进程与子进程的 PTE_W 位；
    if(*pte & PTE_W){
      *pte &= ~PTE_W;
      // 在RSW位置上声明为COW标志位 RSW = 1L << 8
      *pte |= PTE_RSW;
    }
    flags = PTE_FLAGS(*pte);
    // 子进程和父进程映射到同一个物理地址，同时设置子进程页表项标志位
    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
      goto err;
    }
    // 引用计数+1
    add_ref((void*)pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

2. 
```
kernel/trap.c
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
  } else if((r_scause() == 15 || r_scause() == 13) && is_cow_page(r_stval())){ //scause =13、15表示页面错误是由写操作引起的,stval存储异常发生的虚拟地址或数值
    copy_on_write(r_stval());
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
  if(which_dev == 2)
    yield();

  usertrapret();
}
```

```
int
is_cow_page(uint64 va)
{
  struct proc* p = myproc();
  // 让虚拟地址对齐页边界，因为stval可能是页内任意偏移，要对齐页边界
  va = PGROUNDDOWN((uint64)va);
  if(va >= MAXVA){
    return 0;
  }
  // 找到页表项，检查是否为COW
  pte_t* pte = walk(p->pagetable, va, 0);
  if(pte == 0){
    return 0;
  }
  if((va < p->sz) && (*pte & PTE_RSW) && (*pte & PTE_V)){
    return 1;
  }
  return 0;
}
```

```
void
copy_on_write(uint64 va)
{
  struct proc* p = myproc();
  va = PGROUNDDOWN((uint64)va);
  pte_t* pte = walk(p->pagetable, va, 0);
  uint64 pa = PTE2PA(*pte);

  void* new = cow_pa((void*)pa);
  if((uint64)new == 0){
    exit(-1);
  }

  // 取消COW标志，设置可写
  uint64 flags = (PTE_FLAGS(*pte) | PTE_W) & (~PTE_RSW);
  uvmunmap(p->pagetable, va, 1, 0);
  //将子进程页表项映射到新页
  if(mappages(p->pagetable, va, PGSIZE, (uint64)new, flags) == -1){
    kfree(new);
  }
}
```

```
kernel/kalloc.c,
void*
cow_pa(void* pa)
{
  acquire(&ref_lock);
  //如果当前页只引用一次，表示没有其他进程使用该页
  if(refcount[get_ref_id((uint64)pa)] <= 1){
    release(&ref_lock);
    return pa;
  }
  //分配新页
  char* new = kalloc();
  if(new == 0){
    release(&ref_lock);
    return 0;
  }
  // 将父进程的物理页的数据拷贝到新页
  memmove((void*)new, pa, PGSIZE);
  refcount[get_ref_id((uint64)pa)]--;
  release(&ref_lock);
  return (void*)new;
}
```

3. 
```
struct spinlock ref_lock;
int refcount[MAX_REFCOUNT_PAGES];  // 全局引用计数数组，默认

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

//上锁
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_lock, "refcount");
  freerange(end, (void*)PHYSTOP);
}

//根据引用次数释放
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
```

4. 
```
//kernel/vm.c
// Copy from kernel to user.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0){
      return -1;
    }
    if(is_cow_page(va0)){
      copy_on_write(va0);
      pa0 = walkaddr(pagetable, va0);
    }
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

```