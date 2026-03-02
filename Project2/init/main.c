#include "os/list.h"
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
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];


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
static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
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
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */

    typedef struct {
        char name[32];
        int checkpoint;
        int finish;
    } plane_task_info_t;

    plane_task_info_t plane_tasks[] = {
        {"fly1", 10, 60},
        {"fly2", 20, 60},
        {"fly3", 30, 60},
        {"fly4", 40, 60},
        {"fly5", 50, 60}
    };
    int num_plane_tasks = sizeof(plane_tasks) / sizeof(plane_task_info_t);

    char needed_tasks_name[][32]= {"fly1","fly2","fly3","fly4","fly5"};
    uint64_t entry_addr;
    int tasknum = 0;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.list.prev = &pid0_pcb.list;
    pid0_pcb.list.next = &pid0_pcb.list;

    for(int i = 0; i<5 ;i++){
        entry_addr = load_task_img(needed_tasks_name[i]);
        if(entry_addr !=0){
            pcb[tasknum].kernel_sp = (reg_t)(allocKernelPage(1)+PAGE_SIZE);
            pcb[tasknum].user_sp   = (reg_t)(allocUserPage(1)+PAGE_SIZE);
            pcb[tasknum].pid       = tasknum + 1;
            pcb[tasknum].status    = TASK_READY;
            pcb[tasknum].cursor_x  = 0;
            pcb[tasknum].cursor_y  = 0;
        
            int is_plane_task = 0;
            for(int j = 0; j < num_plane_tasks ; j++){
                if(strcmp(needed_tasks_name[i] , plane_tasks[j].name) == 0){
                    pcb[tasknum].fly_stage = 0;
                    pcb[tasknum].checkpoint_dist = plane_tasks[j].checkpoint;
                    pcb[tasknum].finish_dist = plane_tasks[j].finish;
                    pcb[tasknum].progress = 0;
                    pcb[tasknum].workload_setup = 0;
                    pcb[tasknum].remain_length = plane_tasks[j].checkpoint;

                    is_plane_task = 1;
                    break;
                }
            }

            if(is_plane_task){
                pcb[tasknum].status = TASK_BLOCKED;
                Queue_push(&pcb[tasknum].list, &workload_queue);
            }else{
                pcb[tasknum].progress=-1;
                pcb[tasknum].status = TASK_READY;
                Queue_push(&pcb[tasknum].list, &ready_queue);
            }

            init_pcb_stack(pcb[tasknum].kernel_sp, pcb[tasknum].user_sp, entry_addr, &pcb[tasknum]);
            
            if(++tasknum > NUM_MAX_TASK){
                break;
            }

        }
    }

    /* TODO: [p2-task1] remember to initialize 'current_running' */

    current_running = &pid0_pcb;

}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_SLEEP]          = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD]          = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE]          = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR]         = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH]        = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE]   = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK]       = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT]      = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ]       = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE]   = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_SET_SCHE_WORKLOAD] = (long (*)())do_set_sche_workload;
}
/************************************************************/

static void init_task_info(int app_info_loc , int app_info_size)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int start_sector = app_info_loc / 512;
    int blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sector;
    //不用+1，NBYTES2SEC向上取整了
    //createimage中APP_INFO_LOC包含了app_info_size和app_info_loc
    int task_info_addr = TASK_INFO_MEM;
    //app info load to memory:0x52500000
    bios_sd_read(task_info_addr, blocknums, start_sector);
    int start_addr = TASK_INFO_MEM + app_info_loc - start_sector * 512;
    uint64_t *p = (uint64_t *)start_addr;
    memcpy((void *)tasks, (void *)p, app_info_size);
}



int main(int app_info_loc, int app_info_size)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info(app_info_loc, app_info_size);


    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    printk("> [INIT] PCB initialization succeeded.\n");

    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);

    // Init lock mechanism o(´^｀)o
    init_locks();
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    bios_set_timer(get_ticks()+TIMER_INTERVAL);

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
