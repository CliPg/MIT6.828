# kernel/memlayout.h

## 1.QEMU虚拟机的硬件内存布局说明
```
00001000 -- boot ROM, provided by qemu       // 引导ROM的位置
02000000 -- CLINT                            // Core Local Interruptor，本地中断控制器
0C000000 -- PLIC                             // Platform-Level Interrupt Controller，平台级中断控制器
10000000 -- uart0                            // 串口UART
10001000 -- virtio disk                      // 虚拟磁盘的VirtIO接口
80000000 -- boot ROM jumps here in M-mode    // 从ROM跳转到内核加载地址

```
这些地址是 QEMU 模拟 RISC-V 硬件时 固定映射的内存区域。xv6 内核通过这些地址与设备交互，例如读写串口输出、使用虚拟磁盘或处理中断。

## 2.硬件寄存器与中断号定义
```
这些地址是 QEMU 模拟 RISC-V 硬件时 固定映射的内存区域。xv6 内核通过这些地址与设备交互，例如读写串口输出、使用虚拟磁盘或处理中断。
```
当内核要通过 UART 打印字符时，会向 0x10000000 的寄存器写数据。
VirtIO 是一种虚拟化的设备接口，地址是 0x10001000。

## 3.CLINT(本地中断控制器)
```
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8)
```

CLINT 提供定时器和软件中断功能。

CLINT_MTIME：保存从开机以来的时钟周期数。

CLINT_MTIMECMP(hartid)：每个 CPU 核（hart）都有一个比较寄存器，用于触发定时器中断。

## 4.PLIC(平台级中断控制器)
```
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)         // 设置中断优先级
#define PLIC_PENDING (PLIC + 0x1000)       // 查看哪些中断处于挂起状态
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)  // 启用M态中断
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)  // 启用S态中断
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)// M态领取中断
```

这些宏让内核能方便地访问 PLIC 寄存器以处理外设中断。

例如，当 VirtIO 磁盘完成一次 I/O 操作时，会在 PLIC 产生中断。

## 5.内核使用的物理内存范围
```
#define KERNBASE 0x80000000L      // 内核加载的起始地址
#define PHYSTOP (KERNBASE + 128*1024*1024) // xv6 用的最大物理内存：128MB
```

xv6 假设可用内存从 0x80000000 开始，到 PHYSTOP 结束。

entry.S 会从 0x80000000 处启动。

## 6.虚拟内存中的特殊映射
```
#define TRAMPOLINE (MAXVA - PGSIZE)  // 最高地址处的“跳板”页
#define KSTACK(p) (TRAMPOLINE - (p)*2*PGSIZE - 3*PGSIZE) // 每个内核栈映射位置
#define TRAPFRAME (TRAMPOLINE - PGSIZE) // 保存陷入内核时寄存器状态的页
#define USYSCALL (TRAPFRAME - PGSIZE)   // 用户态与内核共享的系统调用信息
```

TRAMPOLINE：特殊页面，用于从用户态切换到内核态的汇编代码（trap handler）。

KSTACK：为每个内核线程分配的栈，放在跳板页下方，防止溢出。

TRAPFRAME：保存用户态寄存器状态，以便从中断/系统调用返回。

USYSCALL：某些实验（如 LAB_PGTBL）中，让用户态直接读取部分内核信息（如 PID）。


# Kernel/vm.c

## 1.内核页表的创建与切换

**kvmmake()：**

调用 kalloc() 申请一页内存作为顶层页表。

映射硬件外设：UART 串口(UART0)、VirtIO 磁盘(VIRTIO0)、PLIC 中断控制器(PLIC)。

映射内核的代码段（只读、可执行）和数据段（可读写）。

映射 trampoline（trap 切换代码）到最高虚拟地址。

为每个进程分配并映射内核栈。

返回内核页表。

**kvminit()：**
调用 kvmmake() 初始化全局变量 kernel_pagetable。

**kvminithart()：**

通过 sfence_vma() 刷新 TLB。

调用 w_satp() 切换到新的页表并启用分页。


## 2.页表遍历与映射
**walk()**

根据 Sv39 虚拟地址格式解析出三级页表索引。

遍历页表，如果 alloc!=0 且某一级缺失，会分配新的页表页。

最终返回最底层页表项（PTE）的指针。

**walkaddr()**

调用 walk() 查找虚拟地址对应的物理地址，只适用于用户页表。

会检查 PTE 是否有效 (PTE_V) 且允许用户访问 (PTE_U)。

**kvmmap() 和 mappages()**

kvmmap()：内核启动时用来映射地址。

mappages()：核心映射函数，将一段虚拟地址区间映射到物理地址区间，并设置访问权限标志。


## 3.用户内存分配和管理
**uvmcreate()**

创建一个空的用户页表。

**uvmfirst()**

用于加载第一个用户进程（init），把 initcode 复制到地址 0 的一页内存中。

**uvmalloc() 和 uvmdealloc()**

uvmalloc()：从 oldsz 扩展到 newsz，分配物理页并映射。

uvmdealloc()：释放多余的页，缩小进程地址空间。

**uvmunmap()**

移除虚拟地址区间的映射，必要时释放物理内存。

**freewalk() 和 uvmfree()**

freewalk()：递归释放页表结构本身（非叶子节点）。

uvmfree()：先释放用户页面，再释放页表结构。

**uvmcopy()**

用于 fork()：将父进程的内存完整复制到子进程，包括数据和页表。

**uvmclear()**

清除某个 PTE 的用户访问位 (PTE_U)，常用于在用户栈下方创建“保护页”（防止栈溢出）。

## 4.内核与用户空间的数据交互
**copyout()**

从内核缓冲区复制数据到用户虚拟地址（用于系统调用返回数据给用户进程）。

**copyin()**

从用户虚拟地址复制数据到内核缓冲区（用于系统调用接收参数）。

**copyinstr()**

类似 copyin()，但会复制以 '\0' 结尾的字符串，且限制最大长度，防止用户提供恶意未终止的字符串。

# riscv.h

```
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access
#define PTE_A (1L << 5)
```

页表项的低10位是标志位

```
9 8 7 6 5 4 3 2 1 0
R S W A G V X W R V

V: 表示这是一个可用的PTE
R：可读
W：可写
X：可执行
U：运行在用户空间的程序页可以访问
G：全局可访问
A：表示是否访问过
D：脏页面
RSW：reserved for supervisor software
```

```
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
物理地址（56位）转化页表项（54位）
物理地址右移12位，可以得到物理页号（PPN），再左移10位
构成一个页表项，但标志位均为0

#define PTE2PA(pte) (((pte) >> 10) << 12)
页表项转化物理地址
页表项右移10位，去掉标志位，左移12位，得到偏移项，但偏移项均为0

#define PTE_FLAGS(pte) ((pte) & 0x3FF)
获取PTE标志位
0x3FF 即 001111111111
保留PTE的低10位（标志位）
```

```
// extract the three 9-bit page table indices from a virtual address.

#define PGSHIFT 12  // bits of offset within a page
#define PXMASK          0x1FF // 9 bits
取出 9 位页表索引。

#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
相应级别页表需要移动的位数

#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)
获取虚拟地址的页表索引（level级的页表）
```






# pageable_t

页表本质是一块连续的内存，里面有512个页表项（PTE）

每一级页表都是一个大小为4KB的页框，一个页框容纳512个条目，每个条目占用8字节。

页表项是页表的基本单元，它记录了虚拟地址的一页是如何映射到物理内存的一页，同时包含访问权限等控制信息。



```
if ((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)
```

用于判断某个PTE是否是一个指向下一级页表的有效页表项。

1. (pte & PTE_V)

- 检查 **PTE 的有效位 (Valid)** 是否为 1。
- 如果为 1，说明这个 PTE 目前是有效的。

2. (pte & (PTE_R|PTE_W|PTE_X))

- 用位或组合了 PTE_R（读）、PTE_W（写）、PTE_X（执行）三个标志位。
- pte & (...) 的结果如果不为 0，表示这是一个 **叶子映射**（直接映射到物理页）。
- 如果结果为 0，则说明它**不是一个叶子映射**，而是**指向下一级页表**。

3. (pte & (PTE_R|PTE_W|PTE_X)) == 0

- 表示：**没有读/写/执行权限** → 它不是用户可直接访问的数据页或代码页。



在 RISC-V Sv39 虚拟内存体系中，**页表项 (PTE)** 的作用有两种：

1. **叶子节点 (Leaf)**：
   - 直接映射到物理内存页。
   - 必须至少有一个权限位 **R(读)**、**W(写)** 或 **X(执行)** 被设置。
   - 因为最终用户空间或内核要用这个物理页读取/写入/执行数据或代码。
2. **中间节点 (Non-leaf / Pointer)**：
   - 不是最终映射，而是指向**下一层页表**。
   - 这种节点只需要被标记为有效 (PTE_V=1)，**但不应有 R/W/X 权限**。
   - 硬件遇到这种情况时，会继续取虚拟地址的下一个 9 位索引，到下一级页表中查找。



# Speed up system calls

一些操作系统（例如 Linux）通过在用户空间和内核之间共享一个只读区域来加速某些系统调用。这样在执行这些系统调用时就不需要频繁切换到内核，从而提高了速度。

在进程中再加入一个属性(kernel/proc.h)

```
// Per-process state
struct proc {
...
  struct trapframe *trapframe; // data page for trampoline.S
  struct usyscall  *usyscall;  //共享页面
  struct context context;      // swtch() here to ...
};
```

用于共享页面

其中usyscall

```
struct usyscall {
  int pid;  // Process ID
};
```



在进程创建页表时，将物理页面映射到USYSCALL

```
if (mappages(pagetable, USYSCALL, PGSIZE, 
             (uint64) (p->usyscall), PTE_R | PTE_U) < 0) {
    // 如果失败就撤回之前trapframe和trampoline，页表的映射
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
}

```

USYSCALL是需要映射到的用户虚拟地址

p->usyscall 内核地址

​                   



# Print a page table

```
page table 0x0000000087f6b000
 ..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
 .. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
 .. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
 .. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
 .. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
 .. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
 ..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
 .. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
 .. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
 .. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
 .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
init: starting sh
```

每一个页表有512项，一个页表4096字节，一个页表项8字节，64位。

通过递归，遍历每一级的512个页表项。

对页表项操作前需要先验证起有效位

```
p & PTE_V
```

然后递归遍历页表项，判断R，W，X位，如果不是全1，则为中间节点，说明该页表项指向的是下一级页表，反之则为叶子节点，递归结束。





# Detect which pages have been accessed 

有些垃圾回收器（一种自动内存管理方式）会从“哪些页面被访问过（读或写）”的信息中获益。在本实验的这一部分中，你需要为 xv6 添加一个新功能：通过检查 RISC-V 页表中的 **访问位（access bit）** 来检测并报告页面的访问情况。RISC-V 的硬件页表遍历器在处理 **TLB miss** 时，会自动在 PTE 中设置这些访问位。

你的任务是实现一个新的系统调用 **pgaccess()**，用于报告哪些页面被访问过。

这个系统调用接收三个参数：

1. **起始虚拟地址**：要检测的第一个用户页面的虚拟地址。
2. **页面数量**：要检测的页面数。
3. **用户缓冲区地址**：用于存放检测结果的缓冲区地址。检测结果应以 **位掩码（bitmask）** 的形式存储，每个页面对应一位，第一个页面对应最低有效位（LSB）。



首先需要在内核申请一页内存暂存结果，并清零该缓冲区。

```
  char* buf = kalloc(); // 在内核分配一页内存暂存结果
  memset(buf, 0, PGSIZE); // 清零缓冲区
```

然后判断page_num的溢出，一页4096B，1B是8bit，可以存储4096*8页的信息

这道题用到了在vm.c提供的walk函数

```
// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
  //PX(level, va)表示level级页表的第几个页表项
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}
```

用于找到虚拟地址va在页表pagetable中对应的页表项的地址（最后一级页表，最后一级页表才存储着虚拟地址和物理地址的映射）

获取页表项后，判断是否访问过。

```
*p & PTE_A
```

如果访问过就需要在缓冲区进行记录。

```
buf[i/8] |= 1 << (i%8); // i表示第几页
*p &= ~PTE_A;
```

这里记录的方法是从缓存空间的buf[0]的第一个bit开始记录第一页。