# large files
目前的xv6文件大小限制为268个块。这268个块有两部分组成。第一部分是12个直接块号，每个直接块号容纳一个块；第二部分是单间接块号，可以容纳256个块号。

现在我们想扩大文件大小，采取的方案是将其中一个直接块号改为双间接块。一个双间接块包含256个单间接块的地址，这样一个双间接块就能指向256*256 = 65536个块。这样总共可以容纳256 * 256 + 256 + 11 = 65803个块。

实现方法是使每个inode中支持一个双重间接块。inode的格式有fs.h中的struct dinode定义。需要特别关注 NDIRECT、NINDIRECT、MAXFILE 以及 struct dinode 的 addrs[] 元素。

磁盘上的 i 节点由结构体 dinode（3676）定义。type 域用来区分文件、目录和特殊文件的 i 节点。如果 type 是0的话就意味着这是一个空闲的 i 节点。nlink 域用来记录指向了这一个 i 节点的目录项，这是用于判断一个 i 节点是否应该被释放的。size 域记录了文件的字节数。addrs 数组用于这个文件的数据块的块号。

inode根据它所在的位置不同有着不同的意思。它可以指磁盘上的记录文件大小、数据块扇区号的数据结构；也可以指内存中的一个inode，包含了一个磁盘上inode的拷贝，以及一些内核需要的附加信息。

要申请一个新的inode，xv6会调用ialloc。ialloc会逐块遍历磁盘上的inode，寻找一个标记为空闲的inode。找到后会把它的type修改掉，最后调用iget从缓存中返回。

inode的数据结构分为在内存中和磁盘中
```
// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

```



```
#define BPB (BSIZE * 8)  // 每个 bitmap block 能表示多少个数据块

static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){ 
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}
```
balloc的作用是在磁盘中分配一个空闲数据块，并返回它的块号。在xv6中磁盘空间由superblock、logblock、inodeblock、bitmapblock、datablock构成。bitmap的大小为1KB，即8192位，每位都表示一个数据块，用0、1表示是否占用。balloc会从bitmap中找到一个空闲的data block。

sb.size表示文件系统的总块数。每次循环会处理一个bitmap block，bread将bitmap block读到内存。然后遍历bitmap block的每一位

在磁盘上查找文件数据的代码位于fs.c的bmap()函数中。
bmap的作用是根据文件的逻辑块号bn，找到相应的磁盘物理块号，必要时自动分配磁盘块。

如果bn在直接块范围内，若未分配磁盘块，则调用balloc分配，最后返回该块号。

如果bn在间接块内，用bread从磁盘中读取这个“指针表”块

对于bmap代码中的`bn -= NDIRECT;`,当bn超过直接块范围内，说明bn在间接块，因此减去NDIRECT就是bn在间接块的索引。

根据题目要求，我们的addr[]前十一个为直接块，第十二个为单间接块，第十三个是双重间接块。
这道题的核心就是初始化双重间接块。

如果双重间接块不存在，就从balloc分配一个数据块。如果存在，就计算当前的bn在哪一个单间接块。

如果该单间接块不存在，就再balloc分配一个数据块。如果存在，就计算当前bn在单间接块的哪一个直接块。

如果该直接块不存在，就再balloc分配一个数据块。


# symbolic links
符号链接 (symlink) 是一种特殊的文件类型，它本身只保存“目标文件的路径字符串”，
当打开它时，内核会“跟随”这个路径去打开真正的目标文件。

想象你有一个文件系统：
```
/home/user/docs/report.txt
```
有一天，你想在桌面（/home/user/Desktop/）上也能打开这个文件，但又不想复制一份，因为复制意味着两个文件，改一个另一个不会变。

于是你创建一个“快捷方式”（Windows）或“别名”（macOS/Linux），比如：
```
/home/user/Desktop/report_link
```
这个 report_link 其实只是一个特殊的文件，里面存着目标路径的字符串：
```
/home/user/docs/report.txt
```
当你打开 report_link 时，系统会自动去打开真正的 /home/user/docs/report.txt。

这就是 符号链接（symbolic link，简称 symlink）。


namei的作用是根据路径名找到并返回对应的inode

