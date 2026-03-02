#include "os/mm.h"
#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <os/string.h>
#include <os/smp.h>
#include <printk.h>
#include <assert.h>


mutex_lock_t mlocks[LOCK_NUM];
int locknum = 0;
barrier_t barrs[BARRIER_NUM];
condition_t conds[CONDITION_NUM];
mailbox_t mbox[MBOX_NUM];

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for(int i = 0 ; i<LOCK_NUM ; i++){
        spin_lock_init(&mlocks[i].lock);
        mlocks[i].block_queue.prev = mlocks[i].block_queue.next = &mlocks[i].block_queue;
        mlocks[i].key = 0;
        mlocks[i].pid = -1;
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    return (atomic_swap(LOCKED,(ptr_t)&lock->status) == UNLOCKED);
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while(atomic_swap(LOCKED,(ptr_t)&lock->status) == LOCKED);
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    for(int i=0; i<LOCK_NUM;i++){
        if(mlocks[i].key == key){
            return i;
        }
    }
    for(int i=0; i < LOCK_NUM; i++){
        if(mlocks[i].key == 0){ 
            mlocks[i].key = key;
            spin_lock_init(&mlocks[i].lock);
            mlocks[i].pid = -1;
            mlocks[i].block_queue.prev = mlocks[i].block_queue.next = &mlocks[i].block_queue;
            return i;
        }
    }
    return -1;

}

void do_mutex_lock_acquire(int mlock_idx)
{
    uint64_t cpu_id = get_current_cpu_id();
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return;
    /* TODO: [p2-task2] acquire mutex lock */
    if(spin_lock_try_acquire(&mlocks[mlock_idx].lock)){
        mlocks[mlock_idx].pid = current_running[cpu_id]->pid;
        return;
    }
    //无法获得锁
    do_block((list_node_t *)&current_running[cpu_id]->list , &mlocks[mlock_idx].block_queue);
}

void do_mutex_lock_release(int mlock_idx)
{
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) return;

    // TODO: [p2-task2] release mutex lock
    list_node_t *p, *head;
    head = &mlocks[mlock_idx].block_queue;
    p = head->next;

    if(p == head){
        mlocks[mlock_idx].pid = -1;
        spin_lock_release(&mlocks[mlock_idx].lock);
    }else{
        mlocks[mlock_idx].pid = get_pcb_from_node(p)->pid;
        do_unblock(p);
    }
}


//------------------------------------------------------------------
//           Barrier
//----------------------------------------------------------------------

void init_barriers(void){
    for(int i=0; i < BARRIER_NUM; i++){
        barrs[i].goal = 0;
        barrs[i].wait_num = 0;
        barrs[i].usage = UNDO;
        barrs[i].wait_list.prev = barrs[i].wait_list.next = &barrs[i].wait_list;
    }
}

int do_barrier_init(int key, int goal){
    for(int i = 0; i < BARRIER_NUM; i++){
        if(barrs[i].usage == TODO && barrs[i].key == key){
            barrs[i].goal = goal;
            return i;
        }
    }
    for(int j = 0; j < BARRIER_NUM; j++){
        if(barrs[j].usage == UNDO){
            barrs[j].usage = TODO;
            barrs[j].key = key;
            barrs[j].goal = goal;
            barrs[j].wait_num = 0;
            barrs[j].wait_list.prev = barrs[j].wait_list.next = &barrs[j].wait_list;
            return j;
        }
    }
    return -1;
}

void do_barrier_wait(int bar_idx){
    uint64_t cpu_id = get_current_cpu_id();
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    barrs[bar_idx].wait_num++;
    if(barrs[bar_idx].wait_num != barrs[bar_idx].goal){
        do_block((list_node_t *)&current_running[cpu_id]->list, &barrs[bar_idx].wait_list);
    }else{
        barrs[bar_idx].wait_num = 0;
        free_block_list(&barrs[bar_idx].wait_list);
    }
}

void do_barrier_destroy(int bar_idx){
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) return;
    free_block_list(&barrs[bar_idx].wait_list);
    barrs[bar_idx].key=0;
    barrs[bar_idx].goal=0;
    barrs[bar_idx].usage=UNDO;
}

//------------------------------------------------------------------
//           Condition
//----------------------------------------------------------------------

void init_conditions(void){
    for(int i = 0; i < CONDITION_NUM; i++){
        conds[i].key = 0; 
        conds[i].usage = UNDO;
        conds[i].wait_list.prev = conds[i].wait_list.next = &conds[i].wait_list;
    }
}

int do_condition_init(int key){
    for(int i=0; i<CONDITION_NUM; i++){
        if(conds[i].usage==TODO && conds[i].key==key){
            return i;
        }
    }
    for(int i=0; i<CONDITION_NUM; i++){
        if(conds[i].usage == UNDO){
            conds[i].usage = TODO;
            conds[i].key = key;
            conds[i].wait_list.prev = conds[i].wait_list.next = &conds[i].wait_list;
            return i;
        }
    }
    return -1;
}

void do_condition_wait(int cond_idx, int mutex_idx){
    uint64_t cpu_id = get_current_cpu_id();
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;
    current_running[cpu_id]->status = TASK_BLOCKED;
    Queue_push((list_node_t *)&current_running[cpu_id]->list, &conds[cond_idx].wait_list);
    do_mutex_lock_release(mutex_idx);   
    do_scheduler();
    do_mutex_lock_acquire(mutex_idx);
}

void do_condition_signal(int cond_idx){
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;
    list_node_t* head, *p;
    head = & conds[cond_idx].wait_list;
    p = head->next;
    if(p!=head)
        do_unblock(p);
}

void do_condition_broadcast(int cond_idx){
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;
    free_block_list(&conds[cond_idx].wait_list);
}

void do_condition_destroy(int cond_idx){
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) return;
    do_condition_broadcast(cond_idx);
    conds[cond_idx].key = 0;
    conds[cond_idx].usage = UNDO;
}


//------------------------------------------------------------------
//           mbox
//----------------------------------------------------------------------

void init_mbox() {
    for (int i = 0; i < MBOX_NUM; i++) {
        for(int j=0;j<MAX_MBOX_LENGTH+1;j++){
                mbox[i].msg[j] = '\0';
        }
        mbox[i].name[0] = '\0';
        mbox[i].wcur = 0;
        mbox[i].rcur = 0;
        mbox[i].user_num = 0;
        mbox[i].wait_mbox_full.prev = mbox[i].wait_mbox_full.next =
            &mbox[i].wait_mbox_full;
        mbox[i].wait_mbox_empty.prev = mbox[i].wait_mbox_empty.next =
            &mbox[i].wait_mbox_empty;
    }
}

int do_mbox_open(char *name) {
    sched_cpu_id = get_current_cpu_id();
    uintptr_t name_kva =
    get_kva((uintptr_t)name, current_running[sched_cpu_id]->pgdir);
    assert(name_kva != 0);

    for (int i = 0; i < MBOX_NUM; i++) {
        if (strcmp(mbox[i].name, (char *)name_kva) == 0) {
            mbox[i].user_num++;
            return i;
        }
    }
    for (int i = 0; i < MBOX_NUM; i++) {
        if (mbox[i].name[0] == '\0') {
        strcpy(mbox[i].name, (char *)name_kva);
        mbox[i].user_num++;
        return i;
        }
    }
    return -1;
}

void do_mbox_close(int mbox_idx) {
    mbox[mbox_idx].user_num--;
    if (mbox[mbox_idx].user_num == 0) {
        mbox[mbox_idx].name[0] = '\0';
        mbox[mbox_idx].wcur = 0;
        mbox[mbox_idx].rcur = 0;
    }
}
#define MODE_W 0
#define MODE_R 1
void myMemcpy(char *dest, char *src, int vcur, int len, int arr_size,
              int mode) {
    int pcur;
    if (mode == MODE_W) {
        for (int i = 0; i < len; i++) {
        pcur = (i + vcur) % arr_size;
        dest[pcur] = src[i];
        }
    } else {
        for (int i = 0; i < len; i++) {
        pcur = (i + vcur) % arr_size;
        dest[i] = src[pcur];
        }
    }
}
int do_mbox_send(int mbox_idx, void *msg, int msg_length) {
    int tmp_wcur;
    int cnt = 0;
    sched_cpu_id = get_current_cpu_id();
    while ((tmp_wcur = mbox[mbox_idx].wcur + msg_length) >
            MAX_MBOX_LENGTH + mbox[mbox_idx].rcur) {
                sched_cpu_id = get_current_cpu_id();
        do_block(&current_running[sched_cpu_id]->list,
                &mbox[mbox_idx].wait_mbox_full);
        cnt++;
    }
    uintptr_t kva_msg =
        get_kva((uintptr_t)msg, current_running[sched_cpu_id]->pgdir);
    // assert(kva_msg != 0);
    if (kva_msg == 0) {
        alloc_limit_page_helper((uintptr_t)msg,
                                current_running[sched_cpu_id]->pgdir);
        
        local_flush_tlb_all();
        kva_msg = get_kva((uintptr_t)msg, current_running[sched_cpu_id]->pgdir);
    }

    myMemcpy(mbox[mbox_idx].msg, (char *)kva_msg, mbox[mbox_idx].wcur, msg_length,
            MAX_MBOX_LENGTH, MODE_W);
    mbox[mbox_idx].wcur = tmp_wcur;
    free_block_list(&mbox[mbox_idx].wait_mbox_empty);
    return msg_length;
}

int do_mbox_recv(int mbox_idx, void *msg, int msg_length) {
    int tmp_rcur;
    int cnt = 0;
    sched_cpu_id = get_current_cpu_id();
    while ((tmp_rcur = mbox[mbox_idx].rcur + msg_length) > mbox[mbox_idx].wcur) {
        sched_cpu_id = get_current_cpu_id();
        do_block(&current_running[sched_cpu_id]->list,
                &mbox[mbox_idx].wait_mbox_empty);
        cnt++;
    }
    sched_cpu_id = get_current_cpu_id();
    uintptr_t kva_msg =
        get_kva((uintptr_t)msg, current_running[sched_cpu_id]->pgdir);
    // assert(kva_msg != 0);
    if (kva_msg == 0) {
        alloc_limit_page_helper((uintptr_t)msg,
                                current_running[sched_cpu_id]->pgdir);
        local_flush_tlb_all();
        kva_msg = get_kva((uintptr_t)msg, current_running[sched_cpu_id]->pgdir);
    }
    myMemcpy((char *)kva_msg, mbox[mbox_idx].msg, mbox[mbox_idx].rcur, msg_length,
            MAX_MBOX_LENGTH, MODE_R);
    mbox[mbox_idx].rcur = tmp_rcur;
    free_block_list(&mbox[mbox_idx].wait_mbox_full);
    return msg_length;
}