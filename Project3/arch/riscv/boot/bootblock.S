#include <asm/biosdef.h>
#include <csr.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc
//bootloader -2

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

//bootloader + 0x1fe - 10
.equ app_info_loc, 0x502001f4

//bootloader + 0x1fe - 18
.equ batch_info_loc, 0x502001ec

.text
.global main

main:
	// fence on all memory and I/O
	fence
    // a0 is mhartid
	bnez a0, secondary

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	la a0 , msg
	li a7 , 9					/*BIOS_PUTSTR*/
	jal bios_func_entry

	// TODO: [p1-task2] call BIOS to read kernel in SD card

	la a0 , kernel		/*load kernel to given address*/
	li t0 , os_size_loc
	lh a1 , 0(t0)
	li a2 , 1			/*start from 2nd block , index is 1*/
	li a7 , 11
	jal bios_func_entry


	// TODO: [p1-task4] load task-related arguments and pass them to kernel
	la t0 , app_info_loc
	lw a0 , 0(t0)		/*load task info to a0*/
	lw a1 , 4(t0)		/*load task num to a1*/

	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	j kernel

	/************************************************************/
secondary:
	/* TODO [P3-task3]: 
	 * 1. Mask all interrupts
	 * 2. let stvec pointer to kernel_main   // 
	 * 3. enable software interrupt for ipi  //支持软件中断
	 */
	//关闭所有中断
	csrw CSR_SIE , zero
	li t0 , SR_SIE
	csrc CSR_SSTATUS , t0

	la t0 , kernel
	csrw CSR_STVEC , t0

	//开软件中断
	li t0 , SIE_SSIE
	csrs CSR_SIE , t0
	//开全局中断
	li t0 , SR_SIE
	csrs CSR_SSTATUS , t0

wait_for_wakeup:
	wfi
	j wait_for_wakeup

	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's JiangYuhan's bootloader...\n\r"

