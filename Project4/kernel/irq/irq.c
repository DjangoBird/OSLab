#include "csr.h"
#include "pgtable.h"
#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <os/smp.h>
#include <os/mm.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

uintptr_t get_pteptr(uintptr_t va, uintptr_t pgdir_va){
    va &= VA_MASK;
    uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    PTE *pgd = (PTE*)pgdir_va;
    if (pgd[vpn2] == 0) return 0;
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pmd[vpn1] == 0) return 0;
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    return (uintptr_t)(pte+vpn0);
}


void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{

    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    if(scause & SCAUSE_IRQ_FLAG){
        irq_table[scause & ~SCAUSE_IRQ_FLAG](regs, stval, scause);
    }else{
        exc_table[scause & ~SCAUSE_IRQ_FLAG](regs, stval, scause);
    }
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    bios_set_timer(get_ticks()+TIMER_INTERVAL);
    do_scheduler();
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    for(int i= 0; i < EXCC_COUNT; i++){
        exc_table[i] = handle_other;
    }

    //只关心来自用户态的系统调用
    exc_table[EXCC_SYSCALL]          = handle_syscall;
    exc_table[EXCC_INST_PAGE_FAULT]  = handle_page_fault;
    exc_table[EXCC_LOAD_PAGE_FAULT]  = handle_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_page_fault;

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    for(int i = 0; i < IRQC_COUNT; i++){
        irq_table[i] = handle_other;
    }

    irq_table[IRQC_S_TIMER] = handle_irq_timer;

    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    // setup_exception();

}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}

void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause){
    uint64_t cpu_id = get_current_cpu_id();
    uintptr_t va = stval;
    uintptr_t pgdir = current_running[cpu_id]->pgdir;

    uintptr_t pte_addr = get_pteptr(va, pgdir);

    if(pte_addr == 0 || (*(PTE *)pte_addr & _PAGE_PRESENT) == 0){
        //TODO:[task3]
        alloc_limit_page_helper(va, pgdir);

    }else{
        printk("Error:Page Fault with valid PTE (Permission Denied?) va=0x%lx\n", va);
        do_exit();
        return;
    }
    local_flush_tlb_all();
}
