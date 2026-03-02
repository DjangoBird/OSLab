#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <mailbox.h>
#include <stdint.h>

#define MSG_LEN 10

void task_worker(int send_mbox, int recv_mbox, int row) {
    char msg[MAX_MBOX_LENGTH] = "Msg";
    char buf[MAX_MBOX_LENGTH];
    int count = 0;

    sys_move_cursor(0, row);
    printf("Worker Started. Step 1: Send (Fill mbox)... Step 2: Recv");

    while (1) {
        count++;
        
        sys_move_cursor(0, row + 1);
        printf("Step 1: Sending msg %d... (Block if full)           ", count);
        
        sys_mbox_send(send_mbox, msg, MSG_LEN);
        
        sys_move_cursor(0, row + 1);
        printf("Step 1: Sent msg %d Success!                        ", count);
    }

    sys_move_cursor(0, row + 2);
    printf("Step 2: Receiving... (Unreachable Code)");
    
    while (1) {
        sys_mbox_recv(recv_mbox, buf, MSG_LEN);
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        int mbox1 = sys_mbox_open("mbox1");
        int mbox2 = sys_mbox_open("mbox2");

        if (argv[1][0] == 'A') {
            // === 任务 A (Core 0) ===
            sys_move_cursor(0, 2);
            printf("--- Task A (Core 0) ---");

            task_worker(mbox1, mbox2, 3);
        } 
        else if (argv[1][0] == 'B') {
            // === 任务 B (Core 1) ===
            sys_move_cursor(0, 6);
            printf("--- Task B (Core 1) ---");

            task_worker(mbox2, mbox1, 7);
        }
        return 0;
    }

    sys_move_cursor(0, 0);
    printf("Deadlock Reproduction: Self-Filling & Blocking\n");
    
    sys_move_cursor(0, 1);
    printf("Main: Launching A and B...\n");


    char *arg_a[] = {"deadlock", "A", NULL};
    char *arg_b[] = {"deadlock", "B", NULL};
    
    int pid_a = sys_exec("deadlock", 2, arg_a);
    int pid_b = sys_exec("deadlock", 2, arg_b);

    sys_taskset(1, 0x1, (void*)(long)pid_a); 
    sys_taskset(1, 0x2, (void*)(long)pid_b); 

    sys_waitpid(pid_a);
    sys_waitpid(pid_b);

    return 0;
}