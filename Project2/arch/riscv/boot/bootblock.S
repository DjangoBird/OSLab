#include <asm/biosdef.h>

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
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's JiangYuhan's bootloader...\n\r"

