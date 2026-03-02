#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <mailbox.h>
#include <stdint.h>

typedef int pthread_t;
int sys_thread_create(uint64_t entry, uint64_t arg);

void pthread_create(pthread_t *thread, void (*start_routine)(void*), void *arg) {
    int tid = sys_thread_create((uint64_t)(uintptr_t)start_routine, (uint64_t)(uintptr_t)arg);
    if (thread) *thread = tid;
}

void pthread_join(pthread_t thread) {
    sys_waitpid(thread);
}

typedef struct {
    int mbox_id;
    int row;
} ThreadArg_t;

#define MSG_LEN 10

void thread_sender(void *arg) {
    ThreadArg_t *t_arg = (ThreadArg_t *)arg;
    int mbox = t_arg->mbox_id;
    int row = t_arg->row;
    char msg[MAX_MBOX_LENGTH] = "Msg";
    int count = 0;
    
    while (1) {
        count++;
        sys_move_cursor(0, row);
        printf("[Sender PID %d] Sending msg %d... (Will block if full)   ", sys_getpid(), count);
        
        sys_mbox_send(mbox, msg, MSG_LEN);
        
        sys_move_cursor(0, row);
        printf("[Sender PID %d] Sent msg %d Success!                     ", sys_getpid(), count);
        
    }
}

void thread_receiver(void *arg) {
    ThreadArg_t *t_arg = (ThreadArg_t *)arg;
    int mbox = t_arg->mbox_id;
    int row = t_arg->row;
    char buf[MAX_MBOX_LENGTH];
    int count = 0;
    
    sys_sleep(2); 

    while (1) {
        count++;

        sys_move_cursor(0, row);
        printf("[Receiver PID %d] Recv msg %d...                         ", sys_getpid(), count);
        
        sys_mbox_recv(mbox, buf, MSG_LEN);
        
        sys_move_cursor(0, row);
        printf("[Receiver PID %d] Recv msg %d Success! (Freed 1 slot)    ", sys_getpid(), count);
        
        sys_sleep(1); 
    }
}


int main(int argc, char *argv[])
{
    if (argc > 1) {
        int mbox1 = sys_mbox_open("mbox1");
        int mbox2 = sys_mbox_open("mbox2");
        pthread_t t1, t2;
        ThreadArg_t arg_send, arg_recv;

        if (argv[1][0] == 'A') {
            // === 任务 A (Core 0) ===
            sys_move_cursor(0, 2);
            printf("--- Task A (Core 0) Started ---");

            // A 向 mbox1 发送 (填满它)
            arg_send.mbox_id = mbox1;
            arg_send.row = 3;
            pthread_create(&t1, thread_sender, &arg_send);
            
            // A 从 mbox2 接收 (解救 B)
            arg_recv.mbox_id = mbox2;
            arg_recv.row = 4;
            pthread_create(&t2, thread_receiver, &arg_recv);
        } 
        else if (argv[1][0] == 'B') {
            // === 任务 B (Core 1) ===
            sys_move_cursor(0, 6);
            printf("--- Task B (Core 1) Started ---");

            // B 向 mbox2 发送 (填满它)
            arg_send.mbox_id = mbox2;
            arg_send.row = 7;
            pthread_create(&t1, thread_sender, &arg_send);
            
            // B 从 mbox1 接收 (解救 A)
            arg_recv.mbox_id = mbox1;
            arg_recv.row = 8;
            pthread_create(&t2, thread_receiver, &arg_recv);
        }
        
        pthread_join(t1);
        pthread_join(t2);
        return 0;
    }

    sys_move_cursor(0, 0);
    printf("Deadlock Solution Test Start... (Auto-fill by threads)\n");

    sys_move_cursor(0, 1);
    printf("Main: Launching A and B...\n");

    char *arg_a[] = {"solve_deadlock", "A", NULL};
    char *arg_b[] = {"solve_deadlock", "B", NULL};
    
    int pid_a = sys_exec("solve_deadlock", 2, arg_a);
    int pid_b = sys_exec("solve_deadlock", 2, arg_b);

    sys_taskset(1, 0x1, (void*)(long)pid_a); 
    sys_taskset(1, 0x2, (void*)(long)pid_b); 

    sys_waitpid(pid_a);
    sys_waitpid(pid_b);

    return 0;
}