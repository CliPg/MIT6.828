write

write 是一个系统调用（在 xv6 或 Unix/Linux 系统中），用于将数据写入一个文件描述符，比如输出到终端、文件或 socket 等。它的使用方式和原理与 C 标准库中的 printf 不一样，是底层的 I/O 操作接口。

write 函数的原型（在 xv6 中）
int write(int fd, const void *buf, int n);
参数说明：
参数	说明
fd	文件描述符（1 表示标准输出，2 表示标准错误输出）
buf	要写入的数据的地址（通常是字符串或字节数组）
n	要写入的字节数

返回值：
成功时，返回写入的字节数；

出错时，返回 -1。

示例：向终端打印字符串
```c
#include "kernel/types.h"
#include "user/user.h"

int main() {
    char *msg = "Hello, xv6!\n";
    write(1, msg, strlen(msg));  // 1 = stdout，写入终端
    exit(0);
}
```
示例：循环打印多个参数（来自 echo.c）
```c
for (int i = 1; i < argc; i++) {
    write(1, argv[i], strlen(argv[i])); // 输出第 i 个参数
    if (i + 1 < argc)
        write(1, " ", 1);               // 参数之间加空格
    else
        write(1, "\n", 1);              // 最后一个加换行
}
```
注意事项：
write 不会自动加 \0 或 \n，它只写你指定的字节数。

如果你写字符串，通常需要配合 strlen() 使用，例如：

write(1, str, strlen(str));
xv6 的文件描述符：

0：标准输入（stdin）

1：标准输出（stdout）

2：标准错误（stderr）

read() 是一个系统调用，它尝试从文件描述符 fd 中读取最多 size 个字节，并把数据放入 buf 指向的缓冲区中。返回值有以下几种情况：

返回 > 0 的值：表示成功读取了这么多字节。

返回 0：表示文件或管道的“写端”已经关闭，而且已经没有更多数据可读了。

返回 -1：出错。

管道的读端和写端都有各自的文件描述符

read(p_left_read_fd, &prime, sizeof(int))表示从p_left_read_fd读数据，写到prime指的地址，大小为sizeofint

fork返回的不是pid， 而是你这个进程是否为子进程，若为子进程，则为0

| 操作                  | 数据流动方向                                | 举例说明              |
| ------------------- | ------------------------------------- | ----------------- |
| `read(fd, buf, n)`  | **从 fd 指向的对象读数据** → **写入你的内存中 buf**   | 比如：从键盘、文件、管道中读取   |
| `write(fd, buf, n)` | **从你的内存中 buf 拿数据** → **写入 fd 指向的对象中** | 比如：打印到终端、写文件、传到管道 |
