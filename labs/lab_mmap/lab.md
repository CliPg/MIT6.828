# 实验：mmap

mmap 和 munmap 系统调用允许 UNIX 程序对其地址空间进行精细控制。它们可用于进程间共享内存、将文件映射到进程地址空间，以及作为用户级缺页机制的一部分，例如课堂上讨论的垃圾回收算法。在本实验中，您将向 xv6 添加 mmap 和 munmap，重点关注内存映射文件。

获取实验所需的 xv6 源代码并检出 mmap 分支：
```
$ git fetch

$ git checkout mmap

$ make clean
```
手册页（运行 man 2 mmap）显示了 mmap 的以下声明：
```
void *mmap(void *addr, size_t len, int prot, int flags,

int fd, off_t offset);
```
mmap 可以通过多种方式调用，但本实验仅需要其与文件内存映射相关的部分功能。您可以假设 addr 始终为零，这意味着内核应该决定要将文件映射到的虚拟地址。mmap 返回该地址，如果映射失败则返回 0xffffffffffffffff。len 是要映射的字节数；它可能与文件长度不同。prot 指示内存映射是否可读、可写和/或可执行；您可以假设 prot 为 PROT_READ 或 PROT_WRITE 或两者兼有。flags 可以是 MAP_SHARED（表示对映射内存的修改应写回文件）或 MAP_PRIVATE（表示不应写回文件）。您无需在 flags 中实现任何其他位。fd 是要映射的文件的打开文件描述符。您可以假设 offset 为零（它是文件中要映射的起始点）。

映射同一个 MAP_SHARED 文件的进程不共享物理页是可以接受的。

手册页（运行 `man 2 munmap`）显示了 `munmap` 的如下声明：

`int munmap(void *addr, size_t len);`

`munmap` 函数应该移除指定地址范围内的 mmap 映射。如果进程修改了内存并将其映射为 `MAP_SHARED`，则应首先将修改写入文件。`munmap` 调用可能只覆盖 mmap 区域的一部分，但您可以假设它会在区域开头、结尾或整个区域取消映射（但不会破坏区域的中间部分）。

您应该实现足够的 mmap 和 `munmap` 功能，以使 `mmaptest` 测试程序能够正常运行。如果 `mmaptest` 没有使用某个 mmap 功能，则无需实现该功能。

完成后，您应该看到以下输出：

```
$ mmaptest
mmap_test starting
test mmap f
test mmap f: OK
test mmap private
test mmap private: OK
test mmap read-only
test mmap read-only: OK
test mmap read/write
test mmap read/write: OK
test mmap dirty
test mmap dirty: OK
test not-mapped unmap
test not-mapped unmap: OK
test mmap two files
test mmap two files: OK
mmap_test: ALL OK
fork_test starting
fork_test OK
mmaptest: all tests succeeded
$ usertests -q
usertests starting
...
ALL TESTS PASSED
$ 
```

**以下是一些提示：**

首先，将 _mmaptest 添加到 UPROGS 中，并添加 mmap 和 munmap 系统调用，以便编译 user/mmaptest.c。目前，只需返回 mmap 和 munmap 的错误即可。我们在 kernel/fcntl.h 中为您定义了 PROT_READ 等。运行 mmaptest，它会在第一次调用 mmap 时失败。

延迟填充页表，仅在发生缺页错误时才填充。也就是说，mmap 不应该分配物理内存或读取文件。相反，这些操作应该在 usertrap 内部（或由 usertrap 调用）的缺页错误处理代码中执行，就像在写时复制实验中那样。延迟填充页表的目的是确保 mmap 映射大文件的速度快，并且能够映射大于物理内存的文件。

跟踪每个进程中 mmap 映射的内容。定义一个与“应用程序的虚拟内存”讲座中描述的 VMA（虚拟内存区域）对应的结构。该结构应该记录 mmap 创建的虚拟内存范围的地址、长度、权限、文件等信息。由于 xv6 内核没有内置的内存分配器，因此可以声明一个固定大小的 VMA 数组，并根据需要从该数组中分配内存。16 个内存应该足够了。

实现 mmap：在进程的地址空间中找到一个未使用的区域来映射文件，并将 VMA 添加到进程的映射区域表中。VMA 应包含指向被映射文件的结构体 file 的指针；mmap 应增加文件的引用计数，以防止文件关闭时结构体丢失（提示：参见 filledup）。运行 mmaptest：第一次 mmap 操作应该成功，但首次访问 mmap 映射的内存将导致缺页并终止 mmaptest 进程。

添加代码，使 mmap 映射区域中发生缺页，从而分配一个物理内存页，将相关文件的 4096 字节读入该页，并将其映射到用户地址空间。使用 readi 函数读取文件，该函数接受一个偏移量参数，用于指定读取文件的偏移量（但您需要锁定/解锁传递给 readi 的 inode）。不要忘记正确设置页面的权限。运行 mmaptest；它应该能够执行第一次 munmap 操作。

实现 munmap：找到地址范围对应的 VMA，并取消映射指定的页面（提示：使用 uvmunmap）。如果 munmap 移除了先前 mmap 的所有页面，则应递减相应结构文件的引用计数。如果一个未映射的页面已被修改，且该文件映射为 MAP_SHARED，则将该页面写回文件。可以参考 filewrite 函数。

理想情况下，你的实现应该只写回程序实际修改过的 MAP_SHARED 页面。RISC-V PTE 中的脏位 (D) 指示页面是否已被写入。然而，mmaptest 不会检查是否写回了未修改的页面；因此，你可以忽略 D 位，直接写回页面。

修改 exit 函数，使其取消映射进程的映射区域，如同调用了 munmap 函数一样。运行 mmaptest；mmap_test 应该通过，但 fork_test 可能不会通过。

修改 fork 函数，确保子进程拥有与父进程相同的映射区域。别忘了增加 VMA 结构体文件的引用计数。在子进程的缺页处理程序中，可以分配一个新的物理页，而不是与父进程共享一个页面。后者虽然更酷，但需要更多的实现工作。运行 mmaptest；它应该同时通过 mmap_test 和 fork_test。

运行 usertests -q 以确保一切仍然正常。