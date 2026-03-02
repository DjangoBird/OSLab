#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <os/string.h>
#include <os/task.h>
#include <screen.h>
#include <csr.h>
#include <os/loader.h>
#include <os/smp.h>

extern void ret_from_exception();

volatile pcb_t * current_running[NR_CPUS];

void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb, int argc, char *argv[]);


pcb_t pcb[NUM_MAX_TASK];
const ptr_t m_pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t m_pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)m_pid0_stack,
    .user_sp = (ptr_t)m_pid0_stack
};

const ptr_t s_pid0_stack = INIT_KERNEL_STACK + 2 * PAGE_SIZE;
pcb_t s_pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)s_pid0_stack,
    .user_sp = (ptr_t)s_pid0_stack
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    // TODO: [p2-task3] Check sleep queue to wake up PCBs
    check_sleeping();

    // TODO: [p2-task1] Modify the current_running[cpu_id] pointer.
    pcb_t * prior_running = (pcb_t *)current_running[cpu_id];
    task_status_t prior_stat = prior_running->status;
    
    if(current_running[cpu_id]->pid != 0){
        // add to the ready queue
        if(prior_running->pid != 0 && prior_stat == TASK_RUNNING){
            prior_running->status = TASK_READY;
            Queue_push(&prior_running->list, &ready_queue);
        }    
        // // 判断前一个进程是否已经退出，是则回收
        // else if(prior_running->pid != 0 && prior_stat==TASK_EXITED)
        //     pcb_release(prior_running);
        // // 还有可能处于blocked状态
    }
    else
        prior_running->status = TASK_READY;

    list_node_t *next_node = find_ready_node();
    
    if (next_node == NULL) {
        current_running[cpu_id] = cpu_id ? &s_pid0_pcb : &m_pid0_pcb;
    } else {
        current_running[cpu_id] = get_pcb_from_node(next_node);
    }

    current_running[cpu_id]->status = TASK_RUNNING;
    current_running[cpu_id]->run_cpu_id = cpu_id;

    // TODO: [p2-task1] switch_to current_running[cpu_id]
    switch_to(prior_running, (pcb_t *)current_running[cpu_id]);
}

void do_sleep(uint32_t sleep_time)
{
    uint64_t cpu_id = get_current_cpu_id();
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running[cpu_id]
    current_running[cpu_id]->status = TASK_BLOCKED;
    Queue_push((list_node_t *)&current_running[cpu_id]->list, &sleep_queue);
    // 2. set the wake up time for the blocked task
    current_running[cpu_id]->wakeup_time = get_timer()+sleep_time;
    // 3. reschedule because the current_running[cpu_id] is blocked.
    do_scheduler();
}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    pcb_t * tmp = get_pcb_from_node(pcb_node);
    tmp->status = TASK_BLOCKED;
    Queue_push(pcb_node, queue);
    do_scheduler();
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    delete_node(pcb_node);
    pcb_t * tmp = get_pcb_from_node(pcb_node);
    tmp->status = TASK_READY;
    Queue_push(pcb_node, &ready_queue);
}

list_node_t* find_ready_node(){
    uint64_t cpu_id = get_current_cpu_id();
    list_node_t *p = ready_queue.next;
    pcb_t *tmp;
    while(1){
        if(p == &ready_queue){
            return cpu_id ? &s_pid0_pcb.list : &m_pid0_pcb.list;
        }
        tmp = get_pcb_from_node(p);
        if(tmp->cpu_mask & (1 << cpu_id)){
            break;
        }
        p = p->next;
    }
    delete_node(p);
    return p;
}

void pcb_release(pcb_t* p){
    p->status = PCB_UNUSED;
    // // 将之从原队列删除
    // delete_node(&(p->list));
    // // 释放等待队列的所有进程
    free_block_list(&(p->wait_list));
    // 释放持有的所有锁
    release_all_lock(p->pid);
}

void Queue_push(list_node_t* node,list_head *head){
    list_node_t *p = head->prev; // tail ptr
    p->next = node;
    node->prev = p;
    node->next = head;
    head->prev = node;           // update tail ptr    
}

void delete_node(list_node_t* node){
    list_node_t* p, *q;
    p = node->prev;
    q = node->next;
    if(p==NULL||q==NULL) return;
    p->next = q;
    q->prev = p;
    node->next = node->prev = NULL; // delete the node completely
}

pcb_t * get_pcb_from_node(list_node_t* node){
    uint64_t cpu_id = get_current_cpu_id();
    for(int i=0;i<NUM_MAX_TASK;i++){
        if(node == &pcb[i].list)
            return &pcb[i];
    }
    return cpu_id ? &s_pid0_pcb : &m_pid0_pcb;
}

void free_block_list(list_node_t* head){    //释放被阻塞的进程
    list_node_t* p, *next;
    for(p=head->next; p!= head; p= next){
        next = p->next;
        do_unblock(p);
    }
}

int search_free_pcb(){  // 查找可用pcb并返回下标，若无则返回-1
    for(int i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].status==PCB_UNUSED)
            return i;
    }
    return -1;
}
// 根据pid释放所有持有的锁
void release_all_lock(pid_t pid){
    for(int i=0; i<LOCK_NUM; i++){
        if(mlocks[i].pid == pid)
            do_mutex_lock_release(i);
    }
}

pid_t do_exec(char *name, int argc, char *argv[]){
    uint64_t cpu_id = get_current_cpu_id();
    int i = search_free_pcb();
    if (i == -1) {
        return 0;
    }

    uint64_t entry_point = load_task_img(name);
    if (entry_point == 0){
        return 0;
    }
    pcb_t *new_pcb = &pcb[i];
    new_pcb->kernel_stack_base = (reg_t)allocKernelPage(1);
    new_pcb->user_stack_base   = (reg_t)allocUserPage(1);
    
    ptr_t user_stack_top   = new_pcb->user_stack_base + PAGE_SIZE;
    ptr_t kernel_stack_top = new_pcb->kernel_stack_base + PAGE_SIZE;
    
    uint64_t current_sp = user_stack_top;
    
    size_t argv_array_size = (argc + 1) * sizeof(char *);
    current_sp -= argv_array_size;
    
    char **argv_ptr_in_stack = (char **)current_sp;

    for(int i = argc -1; i>=0; i--){
        int len = strlen(argv[i]) + 1;//包含'\0'
        current_sp -= len;

        argv_ptr_in_stack[i] = (char *)current_sp;

        strcpy((char *)current_sp , argv[i]);
    }

    argv_ptr_in_stack[argc] = NULL;

    //任务书要求128bit对齐
    uint64_t final_user_sp = ROUNDDOWN(current_sp , 128);

    //pcb初始化
    new_pcb->kernel_sp = kernel_stack_top;
    new_pcb->user_sp = final_user_sp;
    new_pcb->pid = i + 1;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->list.prev = new_pcb->list.next = &new_pcb->list;
    new_pcb->wait_list.prev = new_pcb->wait_list.next = &new_pcb->wait_list;
    new_pcb->cpu_mask = current_running[cpu_id]->cpu_mask;//子进程复制父进程

    init_pcb_stack(
        new_pcb->kernel_stack_base + PAGE_SIZE,
        final_user_sp,
        entry_point,
        new_pcb,
        argc,
        (char **)argv_ptr_in_stack
    );

    Queue_push(&new_pcb->list, &ready_queue);
    return new_pcb->pid;
}

void do_exit(void){
    uint64_t cpu_id = get_current_cpu_id();
    pcb_t *p = (pcb_t *)current_running[cpu_id];
    free_block_list(&p->wait_list);
    p->status = TASK_EXITED;
    release_all_lock(p->pid);
    do_scheduler();
}

int do_kill(pid_t pid){
    uint64_t cpu_id = get_current_cpu_id();
    for(int i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].status!=PCB_UNUSED && pcb[i].pid==pid){
            pcb_t *p = &pcb[i];
            if (p == current_running[cpu_id]) {
                do_exit();
                return 1;
            }
            delete_node(&p->list);
            pcb_release(p);
            return 1;
        }
    }
    return 0;
}

// int do_waitpid(pid_t pid){
//     uint64_t cpu_id = get_current_cpu_id();
//     for(int i=0; i<NUM_MAX_TASK; i++){
//         if(pcb[i].pid == pid){
//              if (pcb[i].status == TASK_EXITED) {
//                 return pid;
//             }
//             if(pcb[i].status != PCB_UNUSED){
//                 do_block(&(current_running[cpu_id]->list), &(pcb[i].wait_list));
//                 return pid;
//             }
//         }
//     }
//     return 0;
// }

int do_waitpid(pid_t pid){
    uint64_t cpu_id = get_current_cpu_id();
    for(int i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].pid == pid && pcb[i].status != PCB_UNUSED){
            pcb_t *child = &pcb[i];
            if (child->status == TASK_EXITED) {
                pcb_release(child);
                return pid;
            }
            do_block((list_node_t *)&(current_running[cpu_id]->list), &(child->wait_list));
            while (child->status == TASK_EXITED) {
                pcb_release(child);
                return pid;
            }
            return 0;
        }
    }
    return 0;
}

void do_process_show(){
    int i;
    static char *stat_str[4]={
        "BLOCKED","RUNNING","READY","EXITED"
    };
    screen_write("[Process table]:\n");
    for(i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].status==PCB_UNUSED)
            continue;
        else if(pcb[i].status==TASK_RUNNING)
            printk("[%d] PID : %d  STATUS : %s Mask: 0x%x Running on the Core : %d  \n", i, pcb[i].pid, stat_str[pcb[i].status], pcb[i].cpu_mask, pcb[i].run_cpu_id);
        else
            printk("[%d] PID : %d  STATUS : %s Mask: 0x%x\n", i, pcb[i].pid, stat_str[pcb[i].status], pcb[i].cpu_mask);
    }
}

pid_t do_getpid(){
    uint64_t cpu_id = get_current_cpu_id();
    return current_running[cpu_id]->pid;
}


pid_t do_taskset(int mode, int mask, void *arg)
{
    if (mask == 0) return -1;
    if (mode == 0) {
        char *name = (char *)arg;
        char *argv[] = { name, NULL };
        int pid = do_exec(name, 1, argv); 
        if (pid == 0) return -1;
        for (int i = 0; i < NUM_MAX_TASK; i++) {
            if (pcb[i].pid == pid) {
                pcb[i].cpu_mask = mask;
                break;
            }
        }
        return pid;
    } 
    else if (mode == 1) {
        int pid = (int)(long)arg;
        for (int i = 0; i < NUM_MAX_TASK; i++) {
            if (pcb[i].status != PCB_UNUSED && pcb[i].pid == pid) {
                pcb[i].cpu_mask = mask;
                return pid;
            }
        }
        return -1;
    }
    return -1;
}



pid_t do_thread_create(uint64_t entry_point, uint64_t arg){
    uint64_t cpu_id = get_current_cpu_id();
    int index = search_free_pcb();
    if(index==-1) return 0;

    pcb_t *new_pcb = &pcb[index];
    pcb_t *parent = (pcb_t *)current_running[cpu_id];

    new_pcb->pid = index + 1;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = parent->cursor_x;
    new_pcb->cursor_y = parent->cursor_y;
    //继承mask
    new_pcb->cpu_mask = parent->cpu_mask;
    new_pcb->kernel_stack_base = (reg_t)allocKernelPage(1);
    new_pcb->kernel_sp = new_pcb->kernel_stack_base + PAGE_SIZE;
    new_pcb->user_stack_base = (reg_t)allocUserPage(1);
    new_pcb->user_sp = new_pcb->user_stack_base + PAGE_SIZE;

    regs_context_t *pt_regs = (regs_context_t *)(new_pcb->kernel_sp - sizeof(regs_context_t));
    for(int i=0;i<32;i++){
        pt_regs->regs[i] = 0; 
    }

    pt_regs->regs[1] = entry_point;
    pt_regs->regs[2] = new_pcb->user_sp;
    pt_regs->regs[4] = (uint64_t)new_pcb;
    pt_regs->regs[10] = (uint64_t)arg;
    pt_regs->sstatus = SR_SPIE;
    pt_regs->sepc = entry_point;

    new_pcb->kernel_sp -= (sizeof(switchto_context_t) + sizeof(regs_context_t));
    switchto_context_t *pt_switchto = (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));

    pt_switchto->regs[0] = (uint64_t)ret_from_exception; // ra
    pt_switchto->regs[1] = new_pcb->kernel_sp;           // sp
 
    new_pcb->list.prev = &new_pcb->list;
    new_pcb->list.next = &new_pcb->list;
    new_pcb->wait_list.prev = &new_pcb->wait_list;
    new_pcb->wait_list.next = &new_pcb->wait_list;

    Queue_push(&new_pcb->list, &ready_queue);

    return new_pcb->pid;
}