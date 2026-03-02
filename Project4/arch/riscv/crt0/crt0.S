#include <asm.h>

/*TASK_MAXNUM = 16*/
#define USER_STACK 0x53000000

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
    /*保存初始栈帧*/
    addi t2, sp, 0
    
    la t0 , __bss_start
    la t1 , __BSS_END__

clear_bss:
    bge t0 , t1 , bss_done
    sw zero , 0(t0)
    add t0 , t0 ,4
    j clear_bss

bss_done:
    /* TODO: [p1-task3] enter main function */
    jal main

    /************************************************************/
    /* TODO: [P3] call sys_exit */
    /************************************************************/
    call sys_exit

// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
