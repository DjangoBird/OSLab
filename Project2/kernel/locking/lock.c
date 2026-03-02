#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];
int locknum = 0;

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for(int i = 0 ; i<LOCK_NUM ; i++){
        spin_lock_init(&mlocks[i].lock);
        mlocks[i].block_queue.prev = mlocks[i].block_queue.next = &mlocks[i].block_queue;
        mlocks[i].key = -1;
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
    if(lock->status == UNLOCKED){
        lock->status = LOCKED;
        return 1;
    }
    return 0;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while(!spin_lock_try_acquire(lock));
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    for(int i=0; i<locknum;i++){
        if(mlocks[i].key == key){
            return i;
        }
    }
    if(locknum < LOCK_NUM){
        mlocks[locknum].key = key;
        return locknum++;//mlock_idx
    }else{
        return -1;
    }
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    if(spin_lock_try_acquire(&mlocks[mlock_idx].lock)) return;

    //无法获得锁
    do_block(&current_running->list , &mlocks[mlock_idx].block_queue);
    pcb_t *prior_running = current_running;
    current_running = get_pcb_from_node(find_ready_node());
    current_running->status = TASK_RUNNING;
    switch_to(prior_running , current_running);
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    list_node_t *p, *head;
    head = &mlocks[mlock_idx].block_queue;
    p = head->next;

    if(p == head){
        spin_lock_release(&mlocks[mlock_idx].lock);
    }else{
        do_unblock(p);
    }
}