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
    li sp , USER_STACK
    addi sp , sp , -16
    sd ra , 0(sp)
    sd t2 , 8(sp)

    /* TODO: [p1-task3] enter main function */
    jal main

    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
    ld ra , 0(sp)
    ld t2 , 8(sp)
    addi sp , t2 , 0
    jalr ra

    /************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
