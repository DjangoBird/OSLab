#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    
     //update sepc
    regs->sepc += 4;

    regs->regs[10] = syscall[regs->regs[17]](      //a7
        regs->regs[10],                            //a0
        regs->regs[11],                            //a1
        regs->regs[12],                            //a2
        regs->regs[13],                            //a3
        regs->regs[14]                             //a4
    );

}
