#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

#define BASE_TIME_SLICE   2
#define MAX_BONUS_SLICE   25


pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack,
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);
LIST_HEAD(workload_queue);
LIST_HEAD(stage_complete_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    // 1. 唤醒睡眠进程
    check_sleeping();
    pcb_t *last_running = current_running;

    // 2. 安置刚刚放弃CPU的任务 (last_running)
    if (last_running->pid != 0) {
        if (last_running->status == TASK_BLOCKED) {
            if (last_running->wakeup_time > 0) {
                Queue_push(&last_running->list, &sleep_queue);
                last_running->wakeup_time = 0;
            } 
            else if (last_running->progress == 100) {
                Queue_push(&last_running->list, &stage_complete_queue);
            }
            else if (last_running->progress != -1) {
                // 飞机仍在飞行 -> 返回“当前赛场”
                Queue_push(&last_running->list, &workload_queue);
            }
        } else if (last_running->status == TASK_RUNNING) {
            // ================== 任务被时钟中断抢占 ==================
            if (last_running->progress != -1) {
                last_running->status = TASK_BLOCKED;
                Queue_push(&last_running->list, &workload_queue);
            } else { 
                last_running->status = TASK_READY;
                Queue_push(&last_running->list, &ready_queue);
            }
        }
    }

    if (ready_queue.next != &ready_queue) {
        list_node_t *next_node = find_ready_node();
        current_running = get_pcb_from_node(next_node);
    } 
    else if (workload_queue.next != &workload_queue) {
        // b. “当前赛场”还有选手，选出最慢的那个继续比赛
        list_node_t *slowest_node = get_slowest_node(&workload_queue);
        delete_node(slowest_node);
        current_running = get_pcb_from_node(slowest_node);
    } 
    else if (stage_complete_queue.next != &stage_complete_queue) {
        // c. 【全局阶段推进】“当前赛场”已空，但“等候区”有人！
        //    这意味着一个阶段已完全结束，是时候开启下一阶段了！
        list_node_t* check_node = stage_complete_queue.next;
        pcb_t* check_pcb = get_pcb_from_node(check_node);
        if(check_pcb->fly_stage >= 1){
            while(stage_complete_queue.next != &stage_complete_queue){
                list_node_t *node = stage_complete_queue.next;
                pcb_t *p = get_pcb_from_node(node);
                p->fly_stage = 0;
                p->workload_setup = 0;
                p->progress = 0;
                p->remain_length = p->checkpoint_dist;
                delete_node(node);
                Queue_push(node, &workload_queue);
            }
        }else {
            while (stage_complete_queue.next != &stage_complete_queue) {
                list_node_t *node = stage_complete_queue.next;
                pcb_t *p = get_pcb_from_node(node);

                // 为新阶段重置状态
                p->fly_stage = 1;
                p->workload_setup = 0;
                p->progress = 0; // 进度清零，重新开始
                p->remain_length = p->finish_dist - p->checkpoint_dist;

                // 将选手送回“赛场”
                delete_node(node);
                Queue_push(node, &workload_queue);
            }
        }
        // 这是一个干净的递归调用
        return do_scheduler();

    } else {
        current_running = &pid0_pcb;
    }

    // 4. 更新状态并切换
    current_running->status = TASK_RUNNING;
    switch_to(last_running, current_running);
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    current_running->status = TASK_BLOCKED;
    // 2. set the wake up time for the blocked task
    current_running->wakeup_time = get_timer() + sleep_time;
    // 3. reschedule because the current_running is blocked.
    do_scheduler();

}

void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    pcb_t *tmp = get_pcb_from_node(pcb_node);
    tmp->status = TASK_BLOCKED;
    Queue_push(pcb_node , queue);
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    pcb_t *tmp = get_pcb_from_node(pcb_node);
    delete_node(pcb_node);
    tmp->status = TASK_READY;
    Queue_push(pcb_node , &ready_queue);
}


void Queue_push(list_node_t *node,list_head *head){
    list_node_t *p = head->prev;
    p->next = node;
    node->prev = p;
    node->next = head;
    head->prev = node;
}

void delete_node(list_node_t *node){
    list_node_t *p = node->prev;
    list_node_t *q = node->next;
    p->next = q;
    q->prev = p;
    node->next = node->prev = NULL;
}

list_node_t* find_ready_node(){
    list_node_t *p = ready_queue.next;
    delete_node(p);
    return p;
}

pcb_t *get_pcb_from_node(list_node_t *node){
    for(int i = 0 ; i < NUM_MAX_TASK ; i++){
        if(&pcb[i].list == node){
            return &pcb[i];
        }
    }
    return &pid0_pcb;            //返回kernel
}

void do_set_sche_workload(int reported_distance)
{
    pcb_t *p = current_running;

    // 使用上一次的剩余距离 p->remain_length 来进行比较
    if ((p->fly_stage == 0 && reported_distance > p->remain_length) || \
        (p->fly_stage == 1 && reported_distance == 0))
    {
        // 已经到达，就把进度强制设为100%，覆盖错误的计算
        p->progress = 100;
    } else{
        reflush_current_running(reported_distance);
    }
    p->status = TASK_BLOCKED;
    do_scheduler();
}

list_node_t *get_slower_node(list_node_t *a_node, list_node_t *b_node)
{
    pcb_t *a_pcb = get_pcb_from_node(a_node);
    pcb_t *b_pcb = get_pcb_from_node(b_node);

    if (a_pcb->progress < b_pcb->progress) return a_node;
    if (b_pcb->progress < a_pcb->progress) return b_node;

    if (a_pcb->remain_length > b_pcb->remain_length) return a_node;
    if (b_pcb->remain_length < a_pcb->remain_length) return b_node;
        
    return a_node;
}


list_node_t *get_slowest_node(list_head *queue_ptr){
    if(queue_ptr->next == queue_ptr){
        return NULL;
    }

    list_node_t *slowest_node = queue_ptr->next;
    list_node_t *curr_node = slowest_node->next;

    while (curr_node != queue_ptr) {
        slowest_node = get_slower_node(slowest_node, curr_node);
        curr_node = curr_node->next;
    }
    return slowest_node;
}

void reflush_current_running(int remain_length_in_stage)
{
    pcb_t *p = current_running;

    // 第一次汇报，根据当前阶段设置 total_length
    if (p->workload_setup == 0) {
        if (p->fly_stage == 0) {
            p->total_length = p->checkpoint_dist;
        } else { // fly_stage == 1
            p->total_length = p->finish_dist - p->checkpoint_dist;
        }
        p->workload_setup = 1;
    }

    // 更新进度
    p->remain_length = remain_length_in_stage;
    if (p->total_length > 0) {
        int travelled = p->total_length - p->remain_length;
        p->progress = (travelled * 100) / p->total_length;
    } else {
        p->progress = 100;
    }
}