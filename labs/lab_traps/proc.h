// Per-process state
struct proc {
  //...
  int alarm_ticks;
  int expired_ticks;
  uint64 fn;
  struct trapframe *trapframe_backup;
  int fn_ret;
};
