// 将文件映射到虚拟内存
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file *file;
  struct proc *p = myproc();
  argaddr(0, &addr);
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &file);
  argint(5, &offset);

  if (!file->writable && (prot & PROT_WRITE) && flags == MAP_SHARED) {
    return -1;
  }
  length = PGROUNDUP(length);
  if (p->sz > MAXVA - length) {
    return -1;
  }
  for (int i = 0; i < VMASIZE; i++) {
    // 查找空闲的vma槽位
    if (p->vma[i].used == 0) {
      p->vma[i].used = 1;
      p->vma[i].addr = p->sz;
      p->vma[i].length = length;
      p->vma[i].prot = prot;
      p->vma[i].flags = flags;
      p->vma[i].fd = fd;
      p->vma[i].file = file;
      p->vma[i].offset = offset;
      filedup(file); // 文件引用计数+1，防止文件提前关闭
      p->sz += length;
      return p->vma[i].addr;
    }
  }
  return -1;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  struct proc *p = myproc();
  struct vma *vma = 0;

  argaddr(0, &addr);
  argint(1, &length);
  addr = PGROUNDDOWN(addr);
  length = PGROUNDUP(length);
  for (int i = 0; i < VMASIZE; i++) {
    if (addr >= p->vma[i].addr || addr < p->vma[i].addr + p->vma[i].length) {
      vma = &p->vma[i];
      break;
    }
  }
  if (vma == 0) {
    return 0;
  }
  if (vma->addr == addr) {
    vma->addr += length;
    vma->length -= length;
    if (vma->flags & MAP_SHARED)
      filewrite(vma->file, addr, length);
    uvmunmap(p->pagetable, addr, length/PGSIZE, 1);
    if (vma->length == 0) {
      fileclose(vma->file);
      vma->used = 0;
    }
  }
  return 0;
}