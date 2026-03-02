#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>
#include <os/string.h>
#include <os/smp.h>

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
//           Condition
//----------------------------------------------------------------------

void init_mbox(){
    for(int i=0; i<MBOX_NUM;i++){
        mbox[i].name[0] = '\0';
        mbox[i].wcur = 0;
        mbox[i].rcur = 0;
        mbox[i].user_num = 0;
        mbox[i].wait_mbox_full.prev = mbox[i].wait_mbox_full.next = &mbox[i].wait_mbox_full; 
        mbox[i].wait_mbox_empty.prev = mbox[i].wait_mbox_empty.next = &mbox[i].wait_mbox_empty; 
    }
}

int do_mbox_open(char *name){
    for(int i=0; i<MBOX_NUM; i++){
        if(strcmp(mbox[i].name,name)== 0){
            mbox[i].user_num ++;
            return i;
        }
    }
    for(int i=0; i<MBOX_NUM; i++){
        if(mbox[i].name[0] == '\0'){
            strcpy(mbox[i].name,name);
            mbox[i].user_num++;
            return i;
        }
    }
    return -1;
}

void do_mbox_close(int mbox_idx){
    mbox[mbox_idx].user_num--;
    if(mbox[mbox_idx].user_num == 0){
        mbox[mbox_idx].name[0]='\0';
        mbox[mbox_idx].wcur = 0;
        mbox[mbox_idx].rcur = 0;
    }
}

// int do_mbox_send(int mbox_idx, void * msg, int msg_length){
//     if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || mbox[mbox_idx].user_num <= 0) {
//         return 0;
//     }

//     mailbox_t *mb = &mbox[mbox_idx];
//     char *buf = (char *)msg;
//     int i;

//     for (i = 0; i < msg_length; i++) {
//         int next_wcur = (mb->wcur + 1) % MAX_MBOX_LENGTH;

//         while (next_wcur == mb->rcur) {
//             do_block(&current_running[cpu_id]->list, &mb->wait_mbox_full);
//             next_wcur = (mb->wcur + 1) % MAX_MBOX_LENGTH; 
//         }

//         mb->msg[mb->wcur] = buf[i];
//         mb->wcur = next_wcur;

//         if (mb->wait_mbox_empty.next != &mb->wait_mbox_empty) {
//             do_unblock(mb->wait_mbox_empty.next);
//         }
//     }

//     return i; 
// }

// int do_mbox_recv(int mbox_idx, void * msg, int msg_length){
//     if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || mbox[mbox_idx].user_num <= 0) {
//         return 0;
//     }

//     mailbox_t *mb = &mbox[mbox_idx];
//     char *buf = (char *)msg;
//     int i;

//     for (i = 0; i < msg_length; i++) {
//         while (mb->wcur == mb->rcur) {
//             do_block(&current_running[cpu_id]->list, &mb->wait_mbox_empty);
//         }

//         buf[i] = mb->msg[mb->rcur];
//         mb->rcur = (mb->rcur + 1) % MAX_MBOX_LENGTH;

//         if (mb->wait_mbox_full.next != &mb->wait_mbox_full) {
//             do_unblock(mb->wait_mbox_full.next);
//         }
//     }

//     return i;
// }


static int get_data_size(mailbox_t *mb) {
    return (mb->wcur - mb->rcur + MAX_MBOX_LENGTH) % MAX_MBOX_LENGTH;
}

static int get_free_space(mailbox_t *mb) {
    return MAX_MBOX_LENGTH - 1 - get_data_size(mb);
}


int do_mbox_send(int mbox_idx, void * msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || mbox[mbox_idx].user_num <= 0) {
        return 0;
    }

    if (msg_length > MAX_MBOX_LENGTH - 1) {
        msg_length = MAX_MBOX_LENGTH - 1;
    }

    mailbox_t *mb = &mbox[mbox_idx];
    char *buf = (char *)msg;

    //whie 保证满足条件
    while (get_free_space(mb) < msg_length) {
        uint64_t cpu_id = get_current_cpu_id();
        do_block((list_node_t *)&current_running[cpu_id]->list, &mb->wait_mbox_full);
    }

    for (int i = 0; i < msg_length; i++) {
        mb->msg[mb->wcur] = buf[i];
        mb->wcur = (mb->wcur + 1) % MAX_MBOX_LENGTH;
    }
    if (mb->wait_mbox_empty.next != &mb->wait_mbox_empty) {
        free_block_list(&mb->wait_mbox_empty);
    }

    return msg_length;
}

int do_mbox_recv(int mbox_idx, void * msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || mbox[mbox_idx].user_num <= 0) {
        return 0;
    }

    mailbox_t *mb = &mbox[mbox_idx];
    char *buf = (char *)msg;

    while (get_data_size(mb) < msg_length) {
        uint64_t cpu_id = get_current_cpu_id();
        do_block((list_node_t *)&current_running[cpu_id]->list, &mb->wait_mbox_empty);
    }

    for (int i = 0; i < msg_length; i++) {
        buf[i] = mb->msg[mb->rcur];
        mb->rcur = (mb->rcur + 1) % MAX_MBOX_LENGTH;
    }

    if (mb->wait_mbox_full.next != &mb->wait_mbox_full) {
        free_block_list(&mb->wait_mbox_full);
    }

    return msg_length;
}