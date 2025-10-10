void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

void
backtrace()
{
  uint64 fp = r_fp(); // 获取当前帧指针

  // fp != 0：检查栈帧指针是否为 0，防止访问非法内存
  // PGROUNDDOWN(fp) == PGROUNDDOWN(r_fp())：确保当前帧和初始栈帧在同一个内核栈页内。
  while (fp != 0 && PGROUNDDOWN(fp) == PGROUNDDOWN(r_fp())) {
    uint64 ra = *(uint64*)(fp-8); // ra存放返回地址，把这个位置的8字节数据读出来
    printf("%p\n", ra);
    fp = *(uint64*)(fp-16); // 上一个栈帧的fp
  }
}