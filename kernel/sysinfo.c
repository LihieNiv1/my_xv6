#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "syscall.h"
#include "sysinfo.h"
#include "proc.h"

extern uint64 get_free_mem(void);
extern uint64 get_num_proc(void);

uint64
sys_sysinfo(void)
{
    struct sysinfo info_p;
    uint64 addr;
    argaddr(0, &addr);
    info_p.nproc = get_num_proc();
    info_p.freemem = get_free_mem();
    struct proc *p = myproc();
    if (copyout(p->pagetable, addr, (char *)&info_p, sizeof(info_p)) < 0)
        return -1;
    return 0;
}
