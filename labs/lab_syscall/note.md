# kernel/proc.h

这段代码负责进程管理，定义了**进程表、CPU管理、进程分配、调度、退出、等待**

## 1. **全局变量与基础数据结构**

```
struct cpu cpus[NCPU];      // 每个 CPU 的状态
struct proc proc[NPROC];    // 进程表：保存所有进程的结构体
struct proc *initproc;      // 指向第一个用户进程(init)
int nextpid = 1;            // 分配 PID 的计数器
struct spinlock pid_lock;   // 保护 nextpid 的自旋锁
struct spinlock wait_lock;  // 父子进程同步用的锁
```

- `proc` 数组保存了整个系统的进程信息。
- `initproc` 是所有孤儿进程的父进程。
- `wait_lock` 确保在 **等待(wait)** 和 **唤醒(wakeup)** 之间不会丢失信号。

## 2. **内核栈映射和初始化**

**proc_mapstacks()**

为每个进程分配内核栈页面，并将其映射到内核页表的高地址，防止用户空间访问：

```
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc(); //从内核的物理内存分配器中申请一页物理内存
    if(pa == 0)
      panic("kalloc"); // 停机并报错
    uint64 va = KSTACK((int) (p - proc)); // 计算进程的内核栈虚拟地址
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W); // 将虚拟地址映射到物理地址pa
  }
}
```

- 每个进程有独立的内核栈，防止进程切换时互相干扰。



```
/*
** kalloc
/*
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
  return (void*)r;
}
```





**procinit()**

初始化进程表：为每个进程结构体初始化锁、状态、内核栈虚拟地址等。

```
// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}
```



## 3. **CPU 和当前进程查询**

- **cpuid()**：读取 `tp` 寄存器返回当前 CPU ID。
- **mycpu()**：返回当前 CPU 对应的 `struct cpu*`。
- **myproc()**：通过 `mycpu()->proc` 获取当前正在运行的进程。

这些函数确保在多核环境下，调度和进程管理始终知道“我是谁”和“我在哪个 CPU 上”。

------

## 4. **进程分配与释放**

### **allocpid()**

分配唯一 PID，使用自旋锁保护：

```
acquire(&pid_lock);
pid = nextpid++;
release(&pid_lock);
```

### **allocproc()**

从 `proc` 表中寻找一个空闲进程 (`UNUSED`)，并完成以下初始化：

1. 分配 PID。
2. 分配 trapframe（保存寄存器上下文）。
3. 创建用户页表（仅包含 trampoline 和 trapframe）。
4. 设置内核上下文，使其从 `forkret` 开始执行。

```
// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

// 查找空闲进程槽位
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```



### **freeproc()**

释放进程的资源，包括：

- trapframe、页表、进程名、父进程指针、文件等。

- `trapframe` 用于**保存和恢复用户态的 CPU 上下文**，包括：

  - 通用寄存器（如 `x1`~`x31` 在 RISC-V 中）。
  - 程序计数器（`epc`，表示异常发生时的指令地址）。
  - 栈指针和状态寄存器（如 `sp`, `sstatus` 等）。

  当中断或系统调用完成后，内核会从 `trapframe` 中恢复这些寄存器，从而让用户程序从中断点**无缝继续执行**。

------

## 5. **用户页表管理**

### **proc_pagetable()**

创建用户页表并映射：

1. **Trampoline**：用于从内核返回用户态（系统调用返回路径）
2. **Trapframe**：用于存放用户态寄存器状态，让陷入内核（trap）时和返回用户态时能够保存/恢复上下文。

```
if(mappages(pagetable, TRAMPOLINE, PGSIZE,
            (uint64)trampoline, PTE_R | PTE_X) < 0){
  uvmfree(pagetable, 0);
  return 0;
}
```

**Trampoline** 是一小段内核汇编代码，用于从内核态安全返回用户态。

`TRAMPOLINE` 是固定的高地址（如 `0x3ffff000`），位于用户虚拟地址空间的最顶端。

将内核中的 `trampoline` 代码（物理地址）映射到用户虚拟地址空间的 `TRAMPOLINE` 处，大小为一页。

```
if(mappages(pagetable, TRAPFRAME, PGSIZE,
            (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmfree(pagetable, 0);
  return 0;
}
```

**Trapframe**：用于存放进程的寄存器状态。

`TRAPFRAME`：是一个虚拟地址，位于 `TRAMPOLINE` 之下。

把进程结构体里的 `p->trapframe`（物理地址）映射到 `TRAPFRAME`。

权限：`PTE_R | PTE_W`

- 可读可写，内核会在 trap 进入/返回时修改这些寄存器值。

如果映射失败：先取消 Trampoline 的映射，再释放页表，避免资源泄漏。

```
         用户虚拟地址空间
  ┌─────────────────────────┐ 高地址
  │  Trampoline 映射        │ <- TRAMPOLINE (执行返回用户态代码)
  ├─────────────────────────┤
  │  Trapframe 映射         │ <- TRAPFRAME (保存寄存器状态)
  ├─────────────────────────┤
  │        （空）           │ <- 还没有用户程序代码和数据
  └─────────────────────────┘ 低地址

```



### **proc_freepagetable()**

反向操作，解除映射并释放内存。

------

## 6. **用户进程初始化**

### **userinit()**

创建第一个用户进程：

1. 调用 `allocproc()` 分配进程结构。
2. 用 `uvmfirst()` 将 `initcode`（汇编程序）加载到用户空间。
3. 设置用户程序入口 `epc=0` (程序计数器）和栈指针。
4. 设置进程名为 `"initcode"`，工作目录为 `/`。
5. 进程状态改为 `RUNNABLE`。

------

## 7. **内存增长**

`growproc()`：调整进程用户空间的大小，用于 `sbrk` 系统调用。

------

## 8. **进程创建与复制**

### **fork()**

1. 调用 `allocproc()` 分配子进程。
2. 使用 `uvmcopy()` 复制父进程的用户内存。
3. 复制寄存器（trapframe），并让子进程的 `a0=0`（fork 返回值）。
4. 复制文件描述符、当前工作目录和进程名。
5. 设置子进程状态为 `RUNNABLE`。

------

## 9. **父子进程管理**

### **reparent()**

当一个进程退出时，将它的所有子进程的父指针改为 `initproc`。

### **exit()**

1. 关闭文件、释放当前工作目录。
2. 将子进程交给 `initproc`。
3. 唤醒父进程（可能在 `wait()` 中睡眠）。
4. 设置状态为 `ZOMBIE` 并进入调度器。

### **wait()**

父进程等待子进程结束：

1. 遍历 `proc` 表寻找子进程。
2. 如果找到 `ZOMBIE` 子进程，回收资源并返回 PID。
3. 如果没有子进程或被杀死，返回 -1。
4. 否则调用 `sleep()` 挂起。

------

## 10. **调度与切换**

### **scheduler()**

核心调度循环：

1. 遍历进程表寻找 `RUNNABLE` 的进程。
2. 切换到它并执行。
3. 当进程主动让出 CPU 或被阻塞时切回调度器。

### **sched()**

保存当前上下文并切换到调度器。

### **yield()**

主动让出 CPU，将进程状态改为 `RUNNABLE` 并调用 `sched()`。

### **forkret()**

子进程第一次被调度运行时调用，用于初始化文件系统并返回用户态。

------

## 11. **睡眠与唤醒**

### **sleep(chan, lk)**

- 释放传入的锁 `lk`。
- 获取自己的进程锁，将状态改为 `SLEEPING` 并调用 `sched()`。
- 被唤醒后恢复环境并重新获取 `lk`。

### **wakeup(chan)**

唤醒所有在 `chan` 上睡眠的进程。

------

## 12. **进程终止与信号**

- **kill(pid)**：标记目标进程为 `killed=1` 并将其从睡眠状态唤醒。
- **setkilled() / killed()**：设置或检查进程是否被杀死。

------

## 13. **内核与用户空间数据拷贝**

- **either_copyout() / either_copyin()**：根据参数选择从用户页表或内核内存拷贝数据，用于系统调用处理。

------

## 14. **调试工具**

### **procdump()**

打印进程信息，用于在控制台调试。



# kernel/entry.S

```
        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
        # 声明 _entry 是全局符号，并放入 .text 段（代码段）。
	    #_entry 会被链接器脚本 kernel.ld 放在内存地址 0x80000000，是所有 CPU 跳			转的入口。
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0 # 将全局符号stack0的地址加载到寄存器sp
        li a0, 1024*4 # 把4096加载到寄存器a0，表示每个CPU栈的大小是4KB
        csrr a1, mhartid # 读取当前hart的硬件ID到a1
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # jump to start() in start.c
        call start
spin:
        j spin

```

QEMU 会把内核加载到物理地址 `0x80000000` 并从这里开始执行。

`_entry` 为每个 CPU 核心分配一个独立的内核栈。

设置好栈指针 **sp** 后，跳到 C 语言实现的 `start()` 函数继续初始化内核。

如果 `start()` 返回（理论上不应返回），就进入 `spin` 死循环。





# kernel/main.c

```
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}

```

这段代码是 **xv6-riscv 内核的入口函数 `main()`**，是所有 CPU（RISC-V 中称为 **hart**）进入 **内核主逻辑** 的第一站。它的核心任务是：**由 hart0（主核）初始化内核子系统，然后唤醒其他 hart，让所有 CPU 进入调度器（scheduler）开始运行进程**。

```
                 所有 CPU 从 _entry 进入 main()
                             │
                ┌────────────┴─────────────┐
           hart0?                           其他 hart
             │                                  │
    控制台/内存/分页初始化                      等待 started=1
    初始化进程、trap、PLIC                      内存屏障
    初始化文件系统                              本地分页/trap/PLIC
    创建第一个用户进程                          打印启动信息
    started=1 通知                              │
             │                                  │
             └────────────┬────────────────────┘
                          ↓
                    调用 scheduler()
                      （多核调度）

```



# kernel/exec.c

它加载 ELF 文件的代码和数据段到新页表中，设置用户栈并传递参数，然后替换当前进程的用户地址空间，最终让进程从新程序的入口地址重新开始执行。

```
exec(path, argv)
  ├─ 打开 path → inode
  ├─ 验证 ELF 文件头
  ├─ 创建新用户页表
  ├─ for 每个 Program Header:
  │    ├─ 分配虚拟内存并映射权限
  │    └─ loadseg: 从磁盘读取到内存
  ├─ 分配用户栈（2 页）
  ├─ 拷贝 argv 字符串到用户栈
  ├─ 设置 trapframe (epc=入口, sp=栈顶, a1=argv)
  ├─ 替换旧页表，释放旧内存
  └─ 返回 argc → 用户态 main(argc, argv)

```













# Using gdb

run make qemu-gdb and then fire up gdb in another window (see the gdb bullet on the [guidance page](https://pdos.csail.mit.edu/6.828/2023/labs/guidance.html)). Once you have two windows open, type in the gdb window:

```
(gdb) file kernel/kernel
gdb-multiarch kernel/kernel

(gdb) target remote :26000 
(gdb) b syscall
Breakpoint 1 at 0x80002142: file kernel/syscall.c, line 243.
(gdb) c
Continuing.
[Switching to Thread 1.2]

Thread 2 hit Breakpoint 1, syscall () at kernel/syscall.c:243
243     {
(gdb) layout src
(gdb) backtrace
```

`kernel/kernel` 是编译出来的 xv6 内核 ELF 可执行文件，里面包含了：

- 所有函数的地址（比如 `syscall` 在哪一行、对应的机器码地址是多少）；
- 调试信息（源码行号和汇编地址的映射）。

只有加载了这个文件，gdb 才知道：

- `syscall` 在 `kernel/syscall.c` 的第几行；
- 它的入口地址是多少；
- 怎样在源代码里单步执行。

调试失败可能是因为gdb一般是xv86的。不支持RISC-V,

可以安装交叉编译工具

```
sudo apt-get install gdb-multiarch
gdb-multiarch kernel/kernel
(gdb) target remote :26000
```



# System call tracing

在这个实验中，你需要为 **xv6** 添加一个系统调用跟踪功能，这将有助于你在之后的实验中调试内核。你要创建一个新的 **trace** 系统调用，用来控制跟踪。这个系统调用需要接收一个整数类型的参数 **mask**，其二进制位表示要跟踪哪些系统调用。

例如，要跟踪 **fork** 系统调用，程序应调用：

```
trace(1 << SYS_fork)
```

其中 **SYS_fork** 是在 `kernel/syscall.h` 中定义的系统调用编号。

你需要修改 xv6 内核，让它在每次系统调用**返回之前**打印一行跟踪信息（**仅当系统调用号在 mask 中被设置时才打印**）。这行信息需要包含：

- 进程 ID
- 系统调用的名称
- 系统调用的返回值

不需要打印系统调用的参数。

`trace` 系统调用应当只对调用它的进程及其之后 fork 出来的子进程生效，而不影响其他进程。

我们已经提供了一个用户态程序 `trace`，它会在启用跟踪的情况下运行另一个程序（见 `user/trace.c`）。当你完成实现后，运行效果应类似如下：

```
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
$
$ trace 2147483647 grep hello README
4: syscall trace -> 0
4: syscall exec -> 3
4: syscall open -> 3
4: syscall read -> 1023
4: syscall read -> 966
4: syscall read -> 70
4: syscall read -> 0
4: syscall close -> 0
$
$ grep hello README
$
$ trace 2 usertests forkforkfork
usertests starting
test forkforkfork: 407: syscall fork -> 408
408: syscall fork -> 409
409: syscall fork -> 410
410: syscall fork -> 411
409: syscall fork -> 412
410: syscall fork -> 413
409: syscall fork -> 414
411: syscall fork -> 415
...
$
```

- 第一个示例中，`trace` 只跟踪 **read** 系统调用（32 = 1<<SYS_read）。
- 第二个示例中，`trace` 跟踪所有系统调用（2147483647 的低 31 位全为 1）。
- 第三个示例中，`grep` 没有被跟踪，所以没有任何输出。
- 第四个示例中，`usertests` 中的 `forkforkfork` 测试，跟踪了所有后代进程的 **fork** 调用。

------

**提示**

1. 在 **Makefile** 中，将 `$U/_trace` 添加到 `UPROGS`。
2. 运行 `make qemu`，编译器会因为缺少用户态的系统调用存根而报错。
   - 需要在 `user/user.h` 中添加 `trace` 的函数声明。
   - 在 `user/usys.pl` 中添加 `trace`。
   - 在 `kernel/syscall.h` 中添加系统调用号。
   - Makefile 会调用 `user/usys.pl` 脚本生成 `user/usys.S`，这个汇编文件使用 RISC-V 的 `ecall` 指令切换到内核。
   - 修正这些问题后，再运行 `trace 32 grep hello README`；它会失败，因为你还没在内核中实现该系统调用。
3. 在 `kernel/sysproc.c` 中添加 `sys_trace()` 函数，通过保存 mask 参数到进程结构体（`kernel/proc.h`）来实现这个新系统调用。
   - 获取系统调用参数的函数在 `kernel/syscall.c`，你可以参考 `kernel/sysproc.c` 中的用法。
4. 修改 `fork()`（位于 `kernel/proc.c`）以便将父进程的 trace mask 复制给子进程。
5. 修改 `kernel/syscall.c` 中的 `syscall()` 函数，在系统调用返回前打印跟踪信息。
   - 你需要添加一个系统调用名称数组，用编号索引找到对应的名称。
6. 如果在 qemu 中直接测试时用例能通过，但运行 `make grade` 时出现超时，尝试在 Athena 环境中测试，因为某些测试对本地机器（特别是 WSL）可能计算量较大。



```
void
syscall(void)
{
  int num; // 存放系统调用号
  struct proc *p = myproc();

  num = p->trapframe->a7; // a7保存了系统调用号
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num](); // 调用对应的syscall，将返回值写入a0
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

这段代码是 **xv6 内核处理系统调用的核心部分**，位于 `syscall()` 函数中。它的作用是：根据系统调用号找到对应的内核函数，执行它，并把返回值写回用户态寄存器，以便用户程序能得到正确的返回值。



# Sysinfo

在这个作业中，你需要新增一个 **sysinfo** 系统调用，用于收集当前正在运行系统的信息。

- 该系统调用接收 **一个参数**：指向 `struct sysinfo` 的指针（定义见 `kernel/sysinfo.h`）。
- 内核需要填写这个结构体中的两个字段：
  1. **freemem**：设置为当前系统空闲内存的字节数。
  2. **nproc**：设置为当前系统中状态不是 `UNUSED` 的进程数量。

我们提供了测试程序 **sysinfotest**；如果它输出

```
sysinfotest: OK
```

说明你的实现是正确的。

------

**一些提示：**

1. 在 **Makefile** 中的 `UPROGS` 变量中加入：

   ```
   $U/_sysinfotest
   ```

2. 运行 `make qemu`；这时 `user/sysinfotest.c` 会编译失败。
    你需要按照上一个作业的步骤添加 `sysinfo` 系统调用。

3. 在 `user/user.h` 中声明 `sysinfo()` 的原型时，
    你需要**预先声明** `struct sysinfo` 的存在：

   ```
   struct sysinfo;
   int sysinfo(struct sysinfo *);
   ```

4. 修复编译问题后，运行 `sysinfotest`；它会失败，
    因为你还没有在内核中实现这个系统调用。

5. **sysinfo** 需要将一个 `struct sysinfo` 从内核拷贝到用户空间；
    参考 `sys_fstat()`（在 `kernel/sysfile.c`）和 `filestat()`（在 `kernel/file.c`），
    学习如何使用 **copyout()** 来完成内核到用户空间的数据拷贝。

6. 为了收集空闲内存大小，在 **kernel/kalloc.c** 中添加一个函数。

7. 为了收集进程数量，在 **kernel/proc.c** 中添加一个函数。
