# Memory allocator (moderate)
这个程序 `user/kalloctest` 用来测试 xv6 的内存分配器：三个进程增长和收缩它们的地址空间，导致多次调用 `kalloc` 和 `kfree`。`kalloc` 和 `kfree` 都会获取 `kmem.lock` 锁。`kalloctest` 会打印（作为 "#test-and-set"）由于尝试获取另一个核心已经持有的锁而在 `acquire` 函数中的循环迭代次数，针对 `kmem` 锁和其他一些锁。`acquire` 中的循环迭代次数大致反映了锁的竞争情况。`kalloctest` 的输出在开始实验之前看起来大致是这样的：
```
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #test-and-set 83375 #acquire() 433015
lock: bcache: #test-and-set 0 #acquire() 1260
--- top 5 contended locks:
lock: kmem: #test-and-set 83375 #acquire() 433015
lock: proc: #test-and-set 23737 #acquire() 130718
lock: virtio_disk: #test-and-set 11159 #acquire() 114
lock: proc: #test-and-set 5937 #acquire() 130786
lock: proc: #test-and-set 4080 #acquire() 130786
tot= 83375
test1 FAIL
start test2
total free number of pages: 32497 (out of 32768)
.....
test2 OK
start test3
child done 1
child done 100000
test3 OK
start test2
total free number of pages: 32497 (out of 32768)
.....
test2 OK
start test3
child done 1
child done 100000
test3 OK
```
你可能会看到与这里展示的不同的计数，以及前五个锁竞争的顺序也不同。

`acquire` 为每个锁维护一个计数，记录了每个锁的 `acquire` 调用次数，以及尝试但未能成功获取锁的循环次数。`kalloctest` 调用一个系统调用，该系统调用会导致内核打印出 `kmem` 和 `bcache` 锁（这是本实验的重点）以及前五个最有竞争的锁的计数。如果存在锁竞争，`acquire` 循环迭代次数将会很大。该系统调用返回 `kmem` 和 `bcache` 锁的循环迭代次数总和。

对于这个实验，你必须使用一个专用的空闲机器，并且具有多个核心。如果你使用的是正在执行其他任务的机器，`kalloctest` 打印出的计数将毫无意义。你可以使用一个专用的 Athena 工作站，或者你自己的笔记本电脑，但不要使用拨号连接的机器。

`kalloctest` 中的锁竞争根本原因在于 `kalloc()` 只有一个空闲链表，并且该链表是由一个单一的锁保护的。为了消除锁竞争，你需要重新设计内存分配器，避免使用单一的锁和链表。基本的思路是为每个 CPU 维护一个空闲链表，每个链表有自己的锁。不同 CPU 上的分配和释放可以并行执行，因为每个 CPU 会操作不同的链表。主要的挑战在于如何处理一个 CPU 的空闲链表为空，但另一个 CPU 的链表有空闲内存的情况；在这种情况下，空链表的 CPU 必须“偷取”另一个 CPU 的空闲链表。偷取可能会引入锁竞争，但这应该是很少发生的。

你的任务是实现每个 CPU 的空闲链表，并在某个 CPU 的空闲链表为空时进行“偷取”。你必须给所有的锁命名，以便它们的名称都以 "kmem" 开头。也就是说，你应该为每个锁调用 `initlock` 并传入以 "kmem" 开头的名称。运行 `kalloctest` 来查看你的实现是否减少了锁竞争。为了检查你的实现是否能够分配所有的内存，运行 `usertests sbrkmuch`。你的输出将类似于下面的内容，其中 `kmem` 锁的竞争总量大大减少，尽管具体数字会有所不同。确保所有 `usertests -q` 的测试都通过。运行 `make grade` 应该显示 `kalloctests` 测试通过。

```
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #test-and-set 0 #acquire() 42843
lock: kmem: #test-and-set 0 #acquire() 198674
lock: kmem: #test-and-set 0 #acquire() 191534
lock: bcache: #test-and-set 0 #acquire() 1242
--- top 5 contended locks:
lock: proc: #test-and-set 43861 #acquire() 117281
lock: virtio_disk: #test-and-set 5347 #acquire() 114
lock: proc: #test-and-set 4856 #acquire() 117312
lock: proc: #test-and-set 4168 #acquire() 117316
lock: proc: #test-and-set 2797 #acquire() 117266
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
start test3
child done 1
child done 100000
test3 OK
$ usertests sbrkmuch
usertests starting
test sbrkmuch: OK
ALL TESTS PASSED
$ usertests -q
...
ALL TESTS PASSED
$
```

**一些提示：**

你可以使用常量 NCPU（定义在 kernel/param.h 中）。
让 freerange 将所有空闲内存分配给当前执行 freerange 的 CPU。
函数 cpuid 会返回当前核心的编号，但只有在中断关闭时调用并使用其结果才是安全的。
你应该使用 push_off() 和 pop_off() 来关闭和重新开启中断。

可以查看 kernel/sprintf.c 中的 snprintf 函数，了解字符串格式化的实现思路。
不过，也可以简单地将所有锁都命名为 "kmem"，这也是可以接受的。

可选地，你可以使用 xv6 的**竞争检测器（race detector）**来运行你的实现：
```
$ make clean
$ make KCSAN=1 qemu
$ kalloctest
...
```
kalloctest 测试可能会失败，但不应该出现数据竞争（race）。
如果 xv6 的竞争检测器检测到了竞争，它会打印两段栈回溯（stack trace），描述竞争发生的位置，大致如下所示。


# Buffer cache (hard)

这个作业的后半部分与前半部分**相互独立**；
也就是说，即使你还没完成前半部分，也可以单独完成这一部分并通过测试。

当多个进程**同时频繁访问文件系统**时，它们很可能会竞争同一个锁：
`bcache.lock` —— 它保护的是磁盘块缓存（定义在 `kernel/bio.c` 中）。

测试程序 `bcachetest` 会创建多个进程，不断读取不同的文件，
从而**制造对 `bcache.lock` 的竞争**。
在你修改代码之前，运行结果会类似于下面这样：

```
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #test-and-set 0 #acquire() 33035
lock: bcache: #test-and-set 16142 #acquire() 65978
--- top 5 contended locks:
lock: virtio_disk: #test-and-set 162870 #acquire() 1188
lock: proc: #test-and-set 51936 #acquire() 73732
lock: bcache: #test-and-set 16142 #acquire() 65978
lock: uart: #test-and-set 7505 #acquire() 117
lock: proc: #test-and-set 6937 #acquire() 73420
tot= 16142
test0: FAIL
start test1
test1 OK
```

你看到的数字可能不同，但总体上，
`bcache.lock` 的 “#test-and-set” 次数会非常高，说明锁竞争严重。


在 `kernel/bio.c` 文件中：

* `bcache.lock` 保护着**磁盘块缓存的共享数据结构**，包括：

  * 缓存块链表（list of cached block buffers）
  * 每个块的引用计数 `b->refcnt`
  * 每个块的标识（`b->dev` 和 `b->blockno`）

也就是说，现在的实现是**所有操作都要抢同一把锁**，
所以多个进程同时访问不同的磁盘块时，也会互相阻塞。


你需要**修改块缓存系统（block cache）**，
使得在运行 `bcachetest` 时，所有与块缓存相关的锁的竞争次数几乎为零。

理想情况下：

* 所有 `bcache` 相关锁的 test-and-set 总次数应该为 **0**。

但允许有少量残余竞争：

* 只要总数 **小于 500** 就可以。

修改要求

* 修改 **`bget`** 和 **`brelse`** 函数（都在 `kernel/bio.c`）。
* 目标是：
  **不同块的并发查找和释放操作不会都去争同一个大锁。**
* 同时必须保持以下不变性（invariants）：

  1. 同一个磁盘块在缓存中最多只有一份（no duplicates）。
  2. 缓冲块数量必须仍然是 **NBUF（30）**，不能随意增加。
  3. 你的修改可以放弃原本的 **LRU（最近最少使用）** 替换策略；
     只要在缓存未命中时，能从任意一个 `refcnt == 0` 的缓存块中选择来使用即可。



完成后：

* 运行 `bcachetest`，输出应类似于：

  ```
  tot = 0
  test0: OK
  ```

  或者至少锁竞争数很低（总和 < 500）。
* `usertests -q` 必须仍然全部通过。
* 最终执行 `make grade` 时，`kalloctests` 和 `bcachetests` 都应通过。



请确保你所有与缓存相关的锁（locks）的名字都以 **“bcache”** 开头。
也就是说，当你调用 `initlock` 初始化锁时，传入的名字应该都以 `"bcache"` 开头。


减少块缓存（block cache）中的锁竞争比减少 `kalloc` 的锁竞争要更困难。
原因是：

* 块缓存（bcache）中的缓冲块是真正 **在多个进程（也就是多个 CPU）之间共享** 的；
* 而 `kalloc` 只是在分配物理页时分配给特定 CPU，可以通过给每个 CPU 自己的分配器来减少竞争。

这种方法（每 CPU 一份）对块缓存是行不通的。
因此，建议你：

> 使用一个 **哈希表（hash table）** 来查找缓存的块，
> 并且**为哈希表的每个桶（bucket）设置一把独立的锁**。

在某些情况下，你的方案仍然可以出现锁冲突，这是允许的：

1. 当两个进程**同时访问相同的块号（block number）**时。

   > （`bcachetest test0` 不会出现这种情况。）

2. 当两个进程**同时在缓存中未命中（miss），并都要找空闲块来替换**时。

   > （`bcachetest test0` 也不会出现这种情况。）

3. 当两个进程同时访问的块号**在你的哈希表中落入同一个桶（bucket）**时。

   > （`bcachetest test0` 可能会出现这种情况，取决于你的设计。）
   > 你可以通过调整哈希表的大小（例如选择不同的桶数量）来减少这种冲突。

`bcachetest` 的 **test1** 会访问比缓存块数量更多的磁盘块，
并会触发更多文件系统路径，因此能充分测试你的缓存系统的性能和正确性。


### 💡 提示与建议

1. 阅读《xv6书》中关于 block cache 的章节（**8.1~8.3**）。
2. 你可以使用固定大小的哈希表，不需要动态扩展。

   > 使用一个**质数（例如 13）**作为桶的数量可以减少哈希冲突。
3. 在哈希表中：

   * 查找某个缓冲块；
   * 若不存在则分配一个新的缓存块；
     这两个步骤必须是**原子操作**。
4. 删除全局缓存链表（`bcache.head` 等），也不需要实现 LRU。

   > 因此，在 `brelse()` 中就**不需要再获取 `bcache.lock`**。
5. 在 `bget()` 中，当缓存未命中时，你可以**选择任何 refcnt == 0 的块**来重用，而不必是“最近最少使用”的那个。
6. 你可能无法在一次加锁中同时：

   * 检查是否命中缓存；
   * 若未命中，再选择一个空闲块。
     因此，当缓存未命中时，你可能需要**释放所有锁，重新开始**。
7. 可以让“查找空闲块并重用”的部分（即 **缓存替换逻辑**）串行执行，不影响并行查找。
8. 某些情况下你可能需要同时持有两把锁，例如：

   * 淘汰一个旧块时；
   * 同时要持有全局的 bcache.lock 和某个桶锁（bucket lock）。
     ⚠️ 请确保避免死锁。
9. 当替换块时，你可能要把 `struct buf` 从一个桶移到另一个桶，因为新的块号哈希到了不同的桶。
   甚至新旧块可能哈希到同一个桶，这种情况要特别小心死锁。
10. 调试建议：

    * 一开始可以保留全局锁 `bcache.lock`（在 bget 的开头/结尾加锁解锁）来保证正确性；
    * 确认逻辑正确后，再移除全局锁，实现真正的并发；
    * 也可以用 `make CPUS=1 qemu` 先单核调试；
    * 使用 xv6 的 race detector（竞态检测器）来找潜在数据竞争。



* 当你运行 `bcachetest test0` 时，**所有涉及 bcache 的锁的自旋次数总和应接近 0**；
  理想情况下是 0，但低于 500 也算通过。
* 同时，`usertests -q` 和 `make grade` 都必须全部通过。

