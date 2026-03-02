/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define M_KERNEL_SP 0xffffffc050501000
#define S_KERNEL_SP 0xffffffc050502000

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero


  // 可以访问用户态页表（用于处理ioremap）
  li t0, SR_SUM
  csrs CSR_SSTATUS, t0

  //salve or not
  csrr t0 , CSR_MHARTID
  bnez t0 , s_start

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */
  la t0 , __bss_start
  la t1 , __BSS_END__

clear_bss:
  bge t0 , t1 , bss_done  /*if start after end , break*/
  sw zero , 0(t0)         /*clear target_address*/
  addi t0 , t0 , 4        /*addr + 4*/
  j clear_bss

bss_done:
  /* TODO: [p1-task2] setup C environment */
  //li sp , KERNEL_STACK
  la tp , m_pid0_pcb
  li sp , M_KERNEL_SP
  j main

s_start:
  la tp , s_pid0_pcb
  li sp , S_KERNEL_SP
  call main

loop:
  wfi
  j loop

END(_start)
