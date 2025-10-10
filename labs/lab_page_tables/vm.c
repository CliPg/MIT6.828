void
vmprint(pagetable_t pagetable, int level)
{
  // 只在顶层打印一次
  if (level == 0) {
    printf("page table %p\n", pagetable);
  }

  // 每个页表有512个页表项
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0)
      continue;

    uint64 pa = PTE2PA(pte);

     for (int j = 0; j < level + 1; j++) {
      printf(" ..");
    }

    if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
      // 中间节点：继续递归
      printf("%d: pte %p pa %p\n", i, pte, pa);
      vmprint((pagetable_t)pa, level + 1);
    } else {
      // 叶子节点：直接映射
      printf("%d: pte %p pa %p\n", i, pte, pa);
    }
  }
}