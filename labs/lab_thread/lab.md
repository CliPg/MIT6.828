# Uthread：线程切换（中等难度）

## problem
在这个练习中，你将设计一个用户级线程系统的上下文切换机制，并实现它。为了让你入手，你的 xv6 已经提供了两个文件 user/uthread.c 和 user/uthread_switch.S，以及 Makefile 中的一条规则用于构建 uthread 程序。

uthread.c 包含了大部分用户级线程包的代码，以及三个简单测试线程的代码。线程包缺少一些创建线程和在线程间切换的代码。

你的任务是制定一个计划，用来创建线程以及保存/恢复寄存器以实现线程切换，并实现该计划。当你完成后，运行 make grade 应该显示你的解决方案通过了 uthread 测试。

完成后，当你在 xv6 上运行 uthread 时，你应该看到如下输出（三个线程可能启动顺序不同）：

$ make qemu
...
$ uthread
thread_a started
thread_b started
thread_c started
thread_c 0
thread_a 0
thread_b 0
thread_c 1
thread_a 1
thread_b 1
...
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
$

这些输出来自三个测试线程，每个线程都有一个循环，它打印一行信息，然后让出 CPU 给其他线程。

目前，如果没有上下文切换代码，你将看不到任何输出。


你需要在 user/uthread.c 中补充 thread_create() 和 thread_schedule() 的代码，并在 user/uthread_switch.S 中实现 thread_switch。

目标之一是确保当 thread_schedule() 第一次运行某个线程时，该线程能够在它自己的栈上执行 thread_create() 传入的函数。

另一个目标是确保 thread_switch 能够保存被切换线程的寄存器，恢复要切换到的线程的寄存器，并返回到后者上次执行的指令位置。

你需要决定在哪里保存/恢复寄存器；修改 struct thread 来保存寄存器是一个不错的方案。你需要在 thread_schedule 中调用 thread_switch；你可以传入任何你需要的参数，但目的是从线程 t 切换到 next_thread。


一些提示：
- thread_switch 只需要保存/恢复被调用者保存（callee-save）的寄存器，为什么？
- 你可以查看用户级线程的汇编代码 uthread.asm，这对调试可能很有帮助。
- 测试时，可以使用 riscv64-linux-gnu-gdb 单步跟踪 thread_switch：

(gdb) file user/_uthread
Reading symbols from user/_uthread...
(gdb) b uthread.c:60

这将在 uthread.c 的第 60 行设置断点。注意，这个断点可能在你运行 uthread 之前就被触发。

当 xv6 shell 运行后，输入 uthread，gdb 会在第 60 行停止。若断点在其他进程触发，可以继续运行，直到在 uthread 进程触发为止。

然后可以使用如下命令检查 uthread 的状态：

(gdb) p/x *next_thread

使用 x 可以查看内存内容：

(gdb) x/x next_thread->stack

你可以直接跳到 thread_switch 开始执行：

(gdb) b thread_switch
(gdb) c

并使用单步执行汇编指令：

(gdb) si

## answer

从一个进程切换到另一个进程，需要先保存当前进程寄存器的状态，并复原要切换的进程的寄存器状态。因此我们在thread结构体中再加入一个字段context用于保存寄存器状态。
```
struct context {
  uint64 ra; // return address 保存函数调用的返回地址
  uint64 sp;

  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```
这里的thread_switch具体是通过汇编实现的，
```
// uthread.c thread_schedule()
thread_switch((uint64)&t->context, (uint64)&next_thread->context);
```
```
// uthread_switch.S
thread_switch:
	/* YOUR CODE HERE */
	sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    # 目标线程
    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)
	ret    /* return to ra */
```
sd即store double，存储双字节，这里就是将寄存器的值存到a0这块内存。
ld即load double，从a1这块内存复原寄存器状态。

创建线程时，会遍历所有线程，看哪个线程空闲，将它设为RUNNABLE，同时要将输入函数的地址放在ra（return address），当调度器第一次“切换到这个线程”执行时，它会“跳转到 func() 开始执行”。。除此之外，还要将栈顶指针指向线程的栈顶。
```
t->context.ra = (uint64)func;
t->context.sp = (uint64)(t->stack) + STACK_SIZE - 1;
```

# 使用线程（中等难度）

在这个作业中，你将通过使用线程（threads）和锁（locks）来探索并行编程（parallel programming），实现一个哈希表（hash table）。
你应该在一台真正的 Linux 或 macOS 电脑上进行此作业（不是在 xv6，也不是在 qemu 中），并且这台电脑应具有多个 CPU 核心。现在大多数笔记本电脑都配有多核处理器。

这个作业使用 UNIX 的 pthread 线程库。你可以通过命令

man pthreads

查看相关手册页，或者在网上查找更多信息（例如 pthreads tutorial 等资源）。


## 实验内容

文件 notxv6/ph.c 中包含了一个简单的哈希表实现，在单线程环境下运行是正确的，但在多线程同时访问时会出错。

在你的 xv6 根目录（比如 ~/xv6-labs-2021）中输入以下命令：
```
$ make ph
$ ./ph 1
```
注意：构建 ph 时，Makefile 使用的是你操作系统自带的 gcc，而不是 xv6 的交叉编译工具。

命令行参数指定了要运行多少个线程去执行哈希表的 put() 和 get() 操作。
运行一段时间后，ph 1 会输出类似下面的结果：

100000 puts, 3.991 seconds, 25056 puts/second
0: 0 keys missing
100000 gets, 3.981 seconds, 25118 gets/second

你看到的数值可能和上面的示例不同（取决于你的电脑性能、多核数量和当前负载等）。



## 输出解释

ph 程序运行了两个基准测试（benchmark）：
1.put 测试：大量向哈希表插入键值对（调用 put()），输出每秒完成的 put 数量。
2.get 测试：从哈希表中获取键值（调用 get()），输出缺失键（missing keys）的数量和每秒完成的 get 数量。


## 多线程测试

你可以通过给 ph 传入大于 1 的参数来让它使用多个线程同时操作哈希表：

$ ./ph 2

可能会看到如下输出：

100000 puts, 1.885 seconds, 53044 puts/second
1: 16579 keys missing
0: 16579 keys missing
200000 gets, 4.322 seconds, 46274 gets/second

解释：
- 第一行说明两个线程并发插入哈希表时，总速率是 53,044 次插入/秒，比单线程时快了约两倍。
这说明有很好的“并行加速比”（parallel speedup）——两个核心完成两倍的工作量。
- 但接下来的两行说明有 16,579 个键缺失（missing）。这表示某些 put() 操作没能正确把键写入哈希表，也就是说出现了并发错误（race condition）。


## 你的任务（第一部分）

查看 notxv6/ph.c 文件中的 put() 和 insert() 函数。

问题：
为什么当两个线程并发运行时会有缺失键，而单线程时不会？

请找出一种可能的事件顺序（sequence of events），说明两个线程同时运行时如何导致一个键缺失。
把你的解释写进 answers-thread.txt 文件中。


修复并发错误（加锁）

为了避免这种问题，你需要在 put() 和 get() 中插入锁操作，保证同时只有一个线程在访问共享的哈希表。

以下是 pthread 提供的相关调用：

pthread_mutex_t lock;            // 声明锁
pthread_mutex_init(&lock, NULL); // 初始化锁
pthread_mutex_lock(&lock);       // 加锁（进入临界区）
pthread_mutex_unlock(&lock);     // 解锁（离开临界区）

当 make grade 显示你通过了 ph_safe 测试时，说明你修复正确。
此时，两个线程时缺失键数量必须为 0。
（此阶段可以不通过 ph_fast 测试。）

记得在程序中调用 pthread_mutex_init() 初始化锁！


测试与性能分析

先用单线程（./ph 1）测试，确认结果正确。
再用双线程（./ph 2）测试，检查：
- 缺失键数量是否为 0（正确性）；
- 性能是否提升（并行加速比）。



## 进一步优化（锁分粒度）

在某些情况下，不同线程执行的 put() 操作不会访问同一个哈希桶（bucket），因此它们其实可以并行运行而不互相影响。

你能修改 ph.c，让这类操作可以并发执行吗？
提示：考虑每个哈希桶单独加一个锁（lock per hash bucket）。

当你修改后：
- 并发安全（ph_safe）测试仍需通过；
- 并行加速测试（ph_fast）也要通过。

ph_fast 测试要求：

当两个线程并发时，puts/second 至少比单线程时快 1.25 倍。

## answer
这个问题是在哈希表的背景下，用锁解决并发问题。

首先看一下哈希表的结构
```
struct entry {
  int key;
  int value;
  struct entry *next;
};
struct entry *table[NBUCKET];
```
table存放了NBUCKET个桶，每个桶存放着entry构成的链表。


接下来看一下怎么使用锁：

声明锁
```
pthread_mutex_t lock;
```
锁的初始化
```
pthread_mutex_init(&lock, NULL);
```
锁的创建和释放
```
pthread_mutex_lock(&lock);
pthread_mutex_unlock(&lock);
```
对于ph_safe，我们只需要构建一个全局锁，在put和get中上锁即可解决并发冲突。而锁的细粒度太大意味着速度会过慢，因为这相当于是串行的执行程序，而非并行。因此我们需要减小锁的细粒度，只对桶进行上锁，这样就可以从原来的每次都需要等待上一个进程完成put/get操作，变为可以在使用的桶不同的前提下，多个线程进行put/get操作。

具体实现
声明一个锁数组，为每个桶提供一个锁
```
pthread_mutex_t locks[NBUCKET];
```
初始化
```
for (int i = 0; i < NBUCKET; i++) {
  pthread_mutex_init(&locks[i], NULL);
}
```
然后在put/get函数前后上锁和解锁。


# 线程同步屏障（barrier）

在这个作业中，你需要实现一个 barrier（屏障）：
它是多线程程序中的一个同步点，所有线程都必须到达这个点后，才能一起继续执行。

你将使用 pthread condition variables（条件变量）来实现，这是一种类似于 xv6 中 sleep / wakeup 机制的线程协调技术。

文件 notxv6/barrier.c 中包含了一个 错误的 barrier 实现。

运行：

$ make barrier
$ ./barrier 2

这里的 2 表示有 2 个线程要在 barrier 上同步（在代码里就是 nthread）。
每个线程在一个循环中执行以下操作：
1.	调用 barrier()；
2.	随机睡眠一小段时间。

但是程序会出现断言错误：

barrier: notxv6/barrier.c:42: thread: Assertion `i == t' failed.

这说明：

有一个线程在另一个线程到达 barrier 之前就提前离开了屏障。


## 目标

你的目标是修复 barrier() 函数，使得：

所有 nthread 个线程都必须到达 barrier() 之后，才能一起继续执行。


可用的 pthread 函数

在实现时，你会用到以下 pthread 条件变量函数：
```
pthread_cond_wait(&cond, &mutex);
// 在 cond 上睡眠：它会自动释放 mutex 锁；
// 被唤醒时会重新获取 mutex 锁。

pthread_cond_broadcast(&cond);
// 唤醒所有在 cond 上睡眠的线程。
```
关键点：
- pthread_cond_wait() 会 自动释放锁 并在被唤醒时重新获得锁；
- pthread_cond_broadcast() 会 一次性唤醒所有等待的线程。


你已经有了：
- barrier_init() 函数；
- 一个 struct barrier 结构体（你可以自由使用它的字段）。

你的任务是实现：

void barrier(void)



## 需要考虑的两个难点
1.	多轮（rounds）的问题
每次所有线程通过 barrier 就算一“轮”。
程序会多次调用 barrier()，所以你要维护 bstate.round，记录当前轮数。
当所有线程到达屏障后，要让 bstate.round++。
2.	线程过快的问题（race condition）
有的线程可能太快，在其他线程还没退出当前 barrier 轮时就已经开始进入下一轮。
所以不能简单地复用 bstate.nthread，否则会混乱。
你需要设计逻辑，确保：
当前轮还没完全结束时，下一轮的线程不会抢先进入。



## 测试方法

请用不同数量的线程进行测试：

./barrier 1
./barrier 2
./barrier 4

所有情况下都应当通过 make grade 的 barrier 测试。

## answer
根据题目意思，要等待每个进程都到达barrier，才能进行到下一轮，从而实现同步。具体流程就是，先对barrier上锁，防止多个线程同时修改nthread的数据，线程到达barrier后，需要等待其他线程，此时将该线程设置为休眠状态，并释放锁，此时其他线程才可以继续前进。当所有线程都到达barrier后，就唤醒所有线程。记得结束的时候还需要释放锁，因为当最后一个线程到达barrier时，并没有释放锁。

