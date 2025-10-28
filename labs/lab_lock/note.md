# Memory allocator (moderate)

这道题锁竞争的根本原因是kalloc只有一个freelist，并由一个单一锁保护，因此当多个进程想要分配内存时，就会出现锁竞争。

所以我们的解决方案是为每个cpu维护一个freelist和lock，同时在某个cpu的freelist为空时，我们可以steal其他cpu的freelist。

我们把kmem声明成一个数组
```
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
```
为每一个cpu分配了一个freelist和一个lock

初始化时，要对每一个kmem初始化锁，同时为freelist分配一块内存空间。

```
void
kfree(void *pa)
{
  struct run *r;
  push_off();
  int id = cpuid();
  pop_off();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}
```
这里要用到提示的cpuid()，返回当前cpu的编号。这里的r是一块申请的内存，然后我们把这块内存塞到这个cpu的freelist的前面。

```
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
```
这段代码将从pa_start向上取整开始，到pa_end的一块连续的内存加入到freelist。

kfree和freerange这两个函数的作用就是将pa_start到pa_end的这块内存加入到当前cpu到freelist。

当需要用到freelist的空闲内存时，我们需要调用kalloc，如果当前cpu的freelist不为空，我们取出第一块内存，freelist前移。如果为空，我们就要遍历所有cpu的freelist，获取它的第一块内存。

以上就是本题的大体思路，加上合适的锁即可通过测试。

# Buffer cache (hard)

问题的背景时，在内存和硬盘间的访问，有一个过渡地带就是缓冲池。当要获取缓冲池中的内存，就会出现锁竞争。这道题和之前的thread有相似之处，都是采用哈希来实现快速访问，但在具体实现上存在一些差异。

bcache和buf的数据结构如下：
```
struct {
  struct spinlock lock;
\  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf buf[NBUFFER];
} bcache[NBUCKET];
```
包含一个锁和buf数组。
相应的哈希函数是`blockno % NBUCKET`。

```
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint timestamp;
};
```
对于访问策略，这里采用了LRU，因此还定义了一个全局变量`static uint global_timestamp`。

初始化时，需要对每一个桶和每一个buf初始化锁，并对每一个buf对时间戳置零。

当想要获取一个指定buf时，我们先在缓冲池中寻找，计算相应键值，在哈希桶遍历寻找，若找到就增加相应引用并更新时间戳。

如果哈希桶没有找到，就遍历所有桶，查找其他桶中的空闲块即引用数为零。

如果没有空闲块，就需要根据lru找到最久未使用的buf。