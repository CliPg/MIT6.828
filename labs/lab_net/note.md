## xv6网络调用链

### 发送调用链
上层若想发送网络包，那就要通过系统调用 sys_write() 来写“文件”。接着，调用 filewrite() ，其在最后会判断写入的文件类型是不是套接字（FD_SOCK）。如果是，那么就调用 sockwrite() ，至此运输层开始。sockwrite() 会给报文分配一个空间用于写数据，接着调用 net_tx_udp() 来进行 UDP 封装，封装完成后进一步调用 net_tx_ip() 进行 IP 封装，随后进一步调用 net_tx_eth() 进行链路层封装。最后，通过调用 e1000_transmit() 来使用 E1000 发送链路帧，这部分需要我们自己实现，后面就是硬件的活了。上述函数都是嵌套调用的，故 xv6 发包的流程如下：

sys_write() -> filewrite() -> sockwrite() -> net_tx_udp() -> net_tx_ip() -> net_tx_eth() -> e1000_transmit()


### 接收调用链
xv6 是通过中断知道 E1000 收到链路帧了。每当 E1000 收到一个链路帧时，就会触发对应的硬件中断。在 usertrap()，会通过 devintr() 来进入硬件中断处理入口。devintr() 会先判断硬件中断的类型，如果是 E1000_IRQ，就说明是 E1000 产生的中断，接着进入 e1000_intr() 处理该中断。随后，调用 e1000_recv() 来处理介绍到的链路帧，这部分需要我们自己实现，个人认为接收比发送要难。 e1000_recv() 中会调用 net_rx() 对链路帧进行解封装，并将其递交给网络层。之后，根据包类型调用 net_rx_ip() 或是 net_rx_arp() 来进一步向上递交，前者是 IP 报文后者是 ARP 报文。对于 IP 报文，进一步调用 net_rx_udp() 将其解封装为 UDP 报文。随后，调用 sockrecvudp()，其会进一步调用 mbufq_pushtail() 来将报文放入一个接收队列。此后，上层就可以通过 sockread() 来从队列中读出该报文了。而对于 ARP 报文，其本身不再有运输层报文，因此不会向上递交，而是会调用 net_tx_arp() 来进行一个 ARP 回复。故 xv6 收包的流程如下：

e1000_intr() -> e1000_recv() -> net_rx() -> net_rx_ip() -> net_rx_udp() -> sockrecvudp() -> mbufq_pushtail()


## E1000收发模型
E1000 使用 DMA 模型（Direct Memory Access），也就是内存直接访问。任何处理网络包的终端都会有一个缓冲区，这是该硬件自带的，E1000 也不例外。但是，E1000 自带的空间毕竟太小了，因此 E1000 将会直接使用主机内存来作缓冲区，也就是内存直接访问。这也是为什么，在实现代码时会发现，所有的包空间都是最终都是通过 kalloc 来分配空间的

下面两个结构体是 Intel E1000 网卡硬件通信中用于描述数据包的“描述符（Descriptor）”结构体。
它们直接对应于网卡硬件手册（Intel® E1000 Software Developer’s Manual）中的数据结构定义。
E1000 网卡通过 DMA（Direct Memory Access，直接内存访问） 与内存交换数据，
而这些描述符结构就是 驱动与网卡之间传递数据包信息的“桥梁”。

**发送描述符（Transmit Descriptor）**
```
struct tx_desc
{
  uint64 addr;    // 待发送数据包在内存中的起始地址（物理地址）
  uint16 length;  // 数据包长度（单位：字节）
  uint8 cso;      // Checksum offset（可忽略，xv6 不用）
  uint8 cmd;      // 命令位（Command bits），控制网卡如何发送
  uint8 status;   // 状态位（Status bits），由网卡填写，表示发送完成等信息
  uint8 css;      // Checksum start（可忽略）
  uint16 special; // 保留字段（不使用）
};
```
功能说明：

每个 tx_desc 描述一次“发送请求”，E1000 根据它来找到并发送一个数据包。
- 驱动（你写的代码）填充 addr、length、cmd；
- E1000 发送完后，会在 status 中设置 E1000_TXD_STAT_DD 标志（表示发送完成）；
- 驱动检测到这个标志后，可以释放对应的 mbuf。

常用 cmd 标志（见手册 §3.3.3）：

|名称|	含义|
|-|-|
|E1000_TXD_CMD_EOP|	表示数据包结束（End of Packet）|
|E1000_TXD_CMD_RS|	请求网卡在发送完成后设置 DD 标志（Report Status）|

通常组合使用：
```
cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
```

**接收描述符（Receive Descriptor）**
```
struct rx_desc
{
  uint64 addr;       // 数据包接收缓冲区的内存地址（网卡 DMA 写入）
  uint16 length;     // 实际接收到的数据包长度
  uint16 csum;       // 校验和（可忽略）
  uint8 status;      // 状态标志位，指示是否有新包
  uint8 errors;      // 错误标志位（可忽略）
  uint16 special;    // 保留字段
};
```
功能说明：

每个 rx_desc 告诉 E1000：

“这里有一块内存空间，你可以把收到的数据包写到这里。”

- 驱动初始化时（e1000_init()）为每个 rx_desc 分配缓冲区 mbuf；
- 当 E1000 收到一个包，就把它 DMA 到 addr 所指内存；
- 并在 status 中设置 E1000_RXD_STAT_DD（Descriptor Done），表示该描述符中有新包；
- 驱动在 e1000_recv() 中检查这个位，取出数据并传给网络协议栈；
- 然后重新分配新的缓冲区并写回 addr，让描述符重新可用。

总结对比：

|字段	|tx_desc (发送)	|rx_desc (接收)|
|-|-|-|
|addr	|要发送的数据包地址|	用于接收数据包的缓冲区地址
|length	|要发送的数据长度	|实际接收到的数据长度
|cmd|	驱动设置的发送控制命令	|无
|status|	网卡设置，表示发送完成	|网卡设置，表示接收完成
|用途|	告诉网卡“去发送这个包”|	告诉网卡“可以把包放这里”

```
struct mbuf {
  struct mbuf  *next; // the next mbuf in the chain
  char         *head; // the current start position of the buffer
  unsigned int len;   // the length of the buffer
  char         buf[MBUF_SIZE]; // the backing store
};
```

它用于存储网络层之间传输的一个数据包或数据片段。

在 xv6 的网络栈中：
- 当操作系统 发送一个网络包 时，包内容就存放在一个 mbuf 中；
- 当网卡 接收到网络包 时，它会 DMA 到某个 mbuf；
- 驱动程序 (e1000.c) 会在 e1000_transmit() 和 e1000_recv() 中读写这些 mbuf。

### 发送模型

要被发送的报文会先被分配一块空间，也就是 mbuf，里面存放着报文的内容，**这些空间在 heap 中，因此是不连续的**。而每一个 mbuf 都会被一个 tx_desc 指向，tx_desc 被连续的放进 tx_ring 队列中。因此，不连续的报文空间因为 tx_ring 的存在，可以被连续地访问。tx_mbufs 是干啥的？tx_mbufs 中的每个元素都是一个 tx_desc 对应的 mbuf 的首址。通俗的将，第 i 个 tx_desc 指向的 mbuf 的首址就是 mbufs[i]。head 指向的缓冲区 buf 的头部。实际上，tx_desc->addr 就是这个 head，而不是 mbuf 的首址，因此，我们额外记录每个 tx_desc 对应的 mbuf 首址，这就是 mbufs 的内容。

- tx_desc[i].addr 是 mbuf 内容（head）的起始地址
- tx_mbufs[i] 是 整个 mbuf 结构体的首地址


### 接收模型
不同于发送模型中 mbuf 的空间需要我们自己分配，在接收模型中，mbufs 的每一个元素对应空间都已经被分配好了，用于存放接收到的报文。mbufs 中何时被放入了数据包我们不管，只需要按照 rx_ring 的顺序依次处理这些 mbuf 并将其递交给上层即可。

相对应的，rx_mbufs 的作用和 tx_mbufs 的作用也不同。发送缓冲区队列 tx_mbufs 初始时全为空指针，而缓冲区 mbuf 实际由 sockwrite() 分配，在最后时绑定到缓冲区队列中, 主要是为了方便后续释放缓冲区。而接收缓冲区队列 rx_mbufs 在初始化时全部都已分配，由内核解封装后释放内存。当 rx_mbufs 中的一个 mbuf 被处理完毕后，需要替换成一个新的缓冲区用于下一次硬件接收数据。

我们需要取得 rx_ring 中首个未处理（未递交）的报文，通过 (regs[E1000_RDT]+1)%RX_RING_SIZE 来确定（tail + 1）。注意，这里不是队列尾，而是队列尾 +1。对于发送模型而言，队列尾是下一个待处理的报文，而对于接收模型而言，队列尾是当前在处理的，而队列尾 +1才是下一个待处理的报文们。


## e1000_transmit

- TDH（Transmit Descriptor Head）：由网卡维护，表示当前处理的位置。
- TDT（Transmit Descriptor Tail）：由驱动维护，表示下一个可用位置。

这两个寄存器是异步更新的：
- 驱动把包写进 tx_ring[TDT]，然后更新 TDT；
- 网卡检测到 TDT 增加，就开始发送新包；
- 发送完后，网卡会设置该描述符的 DD（Descriptor Done）位；
- 网卡更新 TDH（表示它“头部”前进了）。

虽然 TDT 表示“驱动下一个可写位置”，但 驱动只是从寄存器里读取了当前的 TDT 值 ——
它不知道这个位置到底是不是已经被网卡释放（DD置1）。E1000 不会自动清空描述符！驱动必须自己判断这个位置是不是能用了。

把包填入tx_ring和发送包是两个异步的操作，因为tx_ring时一个循环队列，每当填入一个包进入tx_ring，tdt会往前移动，如果循环到起始位置，会判断这个包是否已经被网卡发送了，若已发送，就继续循环前移。

得到tdt，且此位置的包已经发送后，就释放desc指向的内存，将当前需要发送的包存储到tdt的位置，并修改desc的状态。同时把尾指针指向下一位。

## e1000_recv

通过while循环，判断接收队列的尾部是否完成网卡对数据包的接收，将可解封装的数据传递到网络栈，避免队列中可处理数据帧的堆积。