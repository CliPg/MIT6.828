extern pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 start_va;
  int page_num;
  uint64 ua;

  argaddr(0, &start_va);
  argint(1, &page_num);
  argaddr(2, &ua); 


  char* buf = kalloc(); // 在内核分配一页内存暂存结果
  memset(buf, 0, PGSIZE); // 清零缓冲区

  // 一页4096字节，一个bit表示一页，一个字节表示八页
  if (page_num > PGSIZE*8) {
    return -1;
  }

  // 计算bit偏移,让结果在字节数组中右对齐，保证有效bit在每个字节的低位
  /*
  int cnt = (page_num/8 + ((page_num%8) != 0))*8 - page_num;
  for (int i = 0; i < page_num; i++,cnt++) {
    pte_t* p = walk(myproc()->pagetable, start_va+i*PGSIZE, 0);
    if (*p & PTE_A) {
      buf[cnt/8] |= 1 << (cnt%8);
      *p &= ~PTE_A;
    }
  }
  */
  for (int i = 0; i < page_num; i++) {
    pte_t* p = walk(myproc()->pagetable, start_va+i*PGSIZE, 0);
    if (*p & PTE_A) {
      buf[i/8] |= 1 << (i%8);
      *p &= ~PTE_A;
    }
  }

  copyout(myproc()->pagetable, ua, buf, page_num);
  kfree(buf);

  return 0;
}
#endif
