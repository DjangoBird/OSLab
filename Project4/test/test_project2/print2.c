#include <stdio.h>
#include <unistd.h>
<<<<<<< HEAD
// #include <kernel.h>

=======

/**
 * NOTE: bios APIs is used for p2-task1 and p2-task2. You need to change
 * to syscall APIs after implementing syscall in p2-task3!
*/
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
int main(void)
{
    int print_location = 1;

    for (int i = 0;; i++)
    {
        sys_move_cursor(0, print_location);
<<<<<<< HEAD
        printf("> [TASK2] This task is to test scheduler. (%d)", i);
        //sys_yield();
    }
}

// int main(void)
// {
//     int print_location = 1;

//     for (int i = 0;; i++)
//     {
//         kernel_move_cursor(0, print_location);
//         kernel_print("> [TASK] This task is to test scheduler. (%d)", i, 0);
//         kernel_yield();
//     }
// }
=======
        printf("> [TASK] This task is to test scheduler. (%d)", i);
        sys_yield();
    }
}
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
