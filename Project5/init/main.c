#include "os/list.h"
#include "pgtable.h"
#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <os/ioremap.h>
#include <sys/syscall.h>
#include <screen.h>
#include <e1000.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>
#include <os/smp.h>
#include <os/net.h>
#include <plic.h>


#define APP_INFO_ADDR_LOC 0x502001f4
#define SWAP_START ((int *)0xffffffc0502001f0lu)

uint64_t image_end_sec;

extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];

char buffer [SECTOR_SIZE*2];
uint64_t image_end_sector;

void disable_tmp_map(){
    //获取根页表的虚拟地址
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);
    for(uint64_t va = 0x50000000lu; va < 0x51000000lu; va += 0x200000lu){
        va &= VA_MASK;
        uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
        uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
        PTE *pmd = (PTE *)pa2kva(get_pa(pgdir[vpn2]));
        //内核空间是2MB大页，清空VPN1即可
        pmd[vpn1] = 0;
    }
}



static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.
    jmptab[WRITE]           = (long (*)())screen_write;
    jmptab[REFLUSH]         = (long (*)())screen_reflush;
}

/************************************************************/
void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb, int argc, char *argv[])
{
     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));

    pt_regs->regs[1] = (uint64_t)entry_point;
    pt_regs->regs[2] = user_stack;
    pt_regs->regs[4] = (uint64_t)pcb;
    if(pcb->pid == 0){
        pt_regs->sstatus = SR_SPP;
    }else{
        pt_regs->sstatus = SR_SPIE;
    }
    pt_regs->sepc = (uint64_t)entry_point;
    pt_regs->regs[10] = (reg_t)argc;      //a0
    pt_regs->regs[11] = (reg_t)argv;      //a1


    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));

    pcb->kernel_sp = (reg_t)pt_switchto;
    pt_switchto->regs[0] = (uint64_t)ret_from_exception; //ra
    pt_switchto->regs[1] = (reg_t)pcb->kernel_sp;   //sp
    for(int i=2 ; i<14 ; i++){
        pt_switchto->regs[i] = 0;
    }
}

static void init_pcb(void)
{
    // TODO: [p2-task1] load needed tasks and init their corresponding PCB

    m_pid0_pcb.status = TASK_RUNNING;
    m_pid0_pcb.cpu_mask = 0x3;

    m_pid0_pcb.list.prev = &m_pid0_pcb.list;
    m_pid0_pcb.list.next = &m_pid0_pcb.list;
    
    m_pid0_pcb.wait_list.prev = &m_pid0_pcb.wait_list;
    m_pid0_pcb.wait_list.next = &m_pid0_pcb.wait_list;

    s_pid0_pcb.status = TASK_RUNNING;
    s_pid0_pcb.cpu_mask = 0x3;

    s_pid0_pcb.list.prev = &s_pid0_pcb.list;
    s_pid0_pcb.list.next = &s_pid0_pcb.list;
    
    s_pid0_pcb.wait_list.prev = &s_pid0_pcb.wait_list;
    s_pid0_pcb.wait_list.next = &s_pid0_pcb.wait_list;

    for(int i=0; i<NUM_MAX_TASK; i++){
        pcb[i].status = PCB_UNUSED;
        // 初始化时就为每个PCB绑定页表、内核栈与用户栈
        pcb[i].pgdir_ori = pcb[i].pgdir = allocPage(1);
        pcb[i].kernel_sp = (reg_t)(allocPage(1)+PAGE_SIZE);
        pcb[i].user_sp = (reg_t)(allocUSERPage(1)+PAGE_SIZE);
        pcb[i].user_sp_kva = (reg_t)(allocPage(1)+PAGE_SIZE);
    }

    // TODO: [p2-task1] remember to initialize 'current_running'

    current_running[0] = &m_pid0_pcb;
    current_running[1] = &s_pid0_pcb;

}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_EXEC]           = (long (*)())do_exec;
    syscall[SYSCALL_EXIT]           = (long (*)())do_exit;
    syscall[SYSCALL_SLEEP]          = (long (*)())do_sleep;
    syscall[SYSCALL_KILL]           = (long (*)())do_kill;
    syscall[SYSCALL_WAITPID]        = (long (*)())do_waitpid;
    syscall[SYSCALL_PS]             = (long (*)())do_process_show;
    syscall[SYSCALL_GETPID]         = (long (*)())do_getpid;
    syscall[SYSCALL_YIELD]          = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE]          = (long (*)())screen_write;
    syscall[SYSCALL_READCH]         = (long (*)())bios_getchar;
    syscall[SYSCALL_CURSOR]         = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH]        = (long (*)())screen_reflush;
    syscall[SYSCALL_CLEAR]          = (long (*)())screen_clear;
    syscall[SYSCALL_WRITECH]        = (long (*)())screen_write_ch;
    syscall[SYSCALL_GET_TIMEBASE]   = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK]       = (long (*)())get_ticks;
    // syscall[SYSCALL_SET_SCHE_WORKLOAD] = (long (*)())do_set_sche_workload;
    syscall[SYSCALL_LOCK_INIT]      = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ]       = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE]   = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_BARR_INIT]      = (long (*)())do_barrier_init;
    syscall[SYSCALL_BARR_WAIT]      = (long (*)())do_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY]   = (long (*)())do_barrier_destroy;
    syscall[SYSCALL_COND_INIT]      = (long (*)())do_condition_init;
    syscall[SYSCALL_COND_WAIT]      = (long (*)())do_condition_wait;
    syscall[SYSCALL_COND_SIGNAL]    = (long (*)())do_condition_signal;
    syscall[SYSCALL_COND_BROADCAST] = (long (*)())do_condition_broadcast;
    syscall[SYSCALL_MBOX_OPEN]      = (long (*)())do_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE]     = (long (*)())do_mbox_close;
    syscall[SYSCALL_MBOX_SEND]      = (long (*)())do_mbox_send;
    syscall[SYSCALL_MBOX_RECV]      = (long (*)())do_mbox_recv;
    syscall[SYSCALL_TASKSET]        = (long (*)())do_taskset;
    syscall[SYSCALL_THREAD_CREATE]  = (long (*)())do_thread_create;
    syscall[SYSCALL_FREE_MEM]       = (long (*)())get_free_memory;
    syscall[SYSCALL_PIPE_OPEN]      = (long (*)())pipe_open;
    syscall[SYSCALL_PIPE_GIVE]      = (long (*)())pipe_give_pages;
    syscall[SYSCALL_PIPE_TAKE]      = (long (*)())pipe_take_pages;
    
    syscall[SYSCALL_NET_SEND]       = (long (*)())do_net_send;
    syscall[SYSCALL_NET_RECV]       = (long (*)())do_net_recv;
    syscall[SYSCALL_NET_RECV_STREAM]= (long (*)())do_net_recv_stream;
}
/************************************************************/
static void load_tasks(){
    uint64_t entry_addr_phy;
    int i,blocknums;
    int start_sector = 0;
    for (i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].task_name[0] == '\0') {
            break; 
        }
    }
    if (i == 0) return; 
    i--; 

    uint64_t offset_in_sector = tasks[0].start_addr % SECTOR_SIZE;
    entry_addr_phy = TASK_MEM_BASE - offset_in_sector;

    image_end_sector = NBYTES2SEC(tasks[i].task_size + tasks[i].start_addr);
    blocknums = image_end_sector - start_sector;
    //bios_sd_read至多读64个
    while(blocknums > 0){
        int n_to_read = (blocknums > 64) ? 64 : blocknums;
        bios_sd_read(entry_addr_phy, n_to_read, start_sector);
        entry_addr_phy += SECTOR_SIZE * n_to_read;
        start_sector += n_to_read;
        blocknums -= n_to_read;
    }
}

static void init_task_info()
{
    int *app_info_ptr = (int *)pa2kva(APP_INFO_ADDR_LOC);
    int app_info_loc, app_info_size;
    app_info_loc = app_info_ptr[0];
    app_info_size = app_info_ptr[1];
    int start_sector = app_info_loc / SECTOR_SIZE;
    int blocknums = NBYTES2SEC(app_info_size + app_info_loc) - start_sector;
    bios_sd_read(kva2pa((uintptr_t)buffer), blocknums, start_sector);

    uintptr_t buffer_kva = (uintptr_t)buffer;
    uint8_t *tmp = (uint8_t *)(buffer_kva + app_info_loc - start_sector * SECTOR_SIZE);

    memcpy((uint8_t *)tasks, tmp, app_info_size);

    load_tasks();
}


volatile int cpu_init_flag = 0;

int main()
{
    int current_cpu_id = get_current_cpu_id();
    if(current_cpu_id == 0){
        smp_init();
        lock_kernel();
    
        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();

        // Init task information (〃'▽'〃)
        init_task_info();

        int* image_end_sec_addr = SWAP_START;
        image_end_sec = *image_end_sec_addr;

        init_uva_alloc();
        
        // Init Process Control Blocks |•'-'•) ✧
        init_pcb();
        printk("> [INIT] PCB initialization succeeded.\n");

        // Read Flatten Device Tree (｡•ᴗ-)_
        time_base = bios_read_fdt(TIMEBASE);
        e1000 = (volatile uint8_t *)bios_read_fdt(ETHERNET_ADDR);
        uint64_t plic_addr = bios_read_fdt(PLIC_ADDR);
        uint32_t nr_irqs = (uint32_t)bios_read_fdt(NR_IRQS);
        printk("> [INIT] e1000: %lx, plic_addr: %lx, nr_irqs: %lx.\n", e1000, plic_addr, nr_irqs);

        // IOremap
        plic_addr = (uintptr_t)ioremap((uint64_t)plic_addr, 0x4000 * NORMAL_PAGE_SIZE);
        e1000 = (uint8_t *)ioremap((uint64_t)e1000, 8 * NORMAL_PAGE_SIZE);
        printk("> [INIT] IOremap initialization succeeded.\n");

        // Init lock mechanism o(´^｀)o
        init_locks();
        printk("> [INIT] Lock mechanism initialization succeeded.\n");

        // Init barrier o(´^｀)o
        init_barriers();
        printk("> [INIT] Barrier mechanism initialization succeeded.\n");

        // Init condition o(´^｀)o
        init_conditions();
        printk("> [INIT] Condition mechanism initialization succeeded.\n");

        // Init mailbox o(´^｀)o
        init_mbox();
        printk("> [INIT] Mailbox mechanism initialization succeeded.\n");

        // Init interrupt (^_^)
        init_exception();
        printk("> [INIT] Interrupt processing initialization succeeded.\n");


        // TODO: [p5-task4] Init plic
        plic_init(plic_addr, nr_irqs);
        printk("> [INIT] PLIC initialized successfully. addr = 0x%lx, nr_irqs=0x%x\n", plic_addr, nr_irqs);

        // Init network device
        e1000_init();
        printk("> [INIT] E1000 device initialized successfully.\n");

        // Init system call table (0_0)
        init_syscall();
        printk("> [INIT] System call initialized successfully.\n");

        // Init screen (QAQ)
        init_screen();
        printk("> [INIT] SCREEN initialization succeeded.\n");

        // Init system call table (0_0)
        init_syscall();
        printk("> [INIT] System call initialized successfully.\n");
        
        cpu_init_flag++; 

        wakeup_other_hart(NULL);

        // Start shell   Ciallo~ (∠・ω< )⌒☆
        do_exec("shell",0,NULL);

        unlock_kernel();

        while (cpu_init_flag < 2) {
             // 自旋等待
        }

        lock_kernel();
        disable_tmp_map();
        unlock_kernel();

    }else{
        lock_kernel();
        current_running[current_cpu_id]->status = TASK_RUNNING;
        cpu_init_flag++;
        unlock_kernel(); // 释放锁，准备进入调度
    }

    setup_exception();

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    bios_set_timer(get_ticks()+TIMER_INTERVAL);

    if(current_cpu_id == 0){
        lock_kernel();
        printk("> [INIT] CPU 0 initialization succeeded.\n");
        unlock_kernel();
    }
    else{
        lock_kernel();
        printk("> [INIT] CPU 1 initialization succeeded.\n");
        unlock_kernel();
    }
    // unlock_kernel();
    
    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // If you do non-preemptive scheduling, it's used to surrender control
        //do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        enable_preempt();
        asm volatile("wfi");
    }

    return 0;
}
