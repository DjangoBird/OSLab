#include <stdio.h>
#include <assert.h>
#include <unistd.h>
<<<<<<< HEAD
// #include <kernel.h>
=======
>>>>>>> 347ac9667be18988db514b319f02a536414669b6

// LOCK2_KEY is the key of this task. You can define it as you wish.
// We use 42 here because it is "Answer to the Ultimate Question of Life,
// the Universe, and Everything" :)
#define LOCK2_KEY 42

static char blank[] = {"                                             "};

<<<<<<< HEAD
=======
/**
 * NOTE: bios APIs is used for p2-task1 and p2-task2. You need to change
 * to syscall APIs after implementing syscall in p2-task3!
*/
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
int main(void)
{
    int print_location = 3;
    int mutex_id = sys_mutex_init(LOCK2_KEY);
    assert(mutex_id >= 0);

    while (1)
    {
        sys_move_cursor(0, print_location);
        printf("%s", blank);

        sys_move_cursor(0, print_location);
        printf("> [TASK] Applying for a lock.\n");

<<<<<<< HEAD
        //sys_yield();
=======
        sys_yield();
>>>>>>> 347ac9667be18988db514b319f02a536414669b6

        sys_mutex_acquire(mutex_id);

        for (int i = 0; i < 5; i++)
        {
            sys_move_cursor(0, print_location);
            printf("> [TASK] Has acquired lock and running.(%d)\n", i);
<<<<<<< HEAD
            //sys_yield();
=======
            sys_yield();
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
        }

        sys_move_cursor(0, print_location);
        printf("%s", blank);

        sys_move_cursor(0, print_location);
        printf("> [TASK] Has acquired lock and exited.\n");

        sys_mutex_release(mutex_id);

<<<<<<< HEAD
        //sys_yield();
=======
        sys_yield();
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
    }

    return 0;
}
<<<<<<< HEAD

// int main(void)
// {
//     int print_location = 3;
//     int mutex_id = kernel_mutex_init(LOCK2_KEY);
//     assert(mutex_id >= 0);

//     while (1)
//     {
//         kernel_move_cursor(0, print_location);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, print_location);
//         kernel_print("> [TASK] Applying for a lock.\n", 0, 0);

//         kernel_yield();

//         kernel_mutex_acquire(mutex_id);

//         for (int i = 0; i < 5; i++)
//         {
//             kernel_move_cursor(0, print_location);
//             kernel_print("> [TASK] Has acquired lock and running.(%d)\n", i, 0);
//             kernel_yield();
//         }

//         kernel_move_cursor(0, print_location);
//         kernel_print("%s", (long)blank, 0);

//         kernel_move_cursor(0, print_location);
//         kernel_print("> [TASK] Has acquired lock and exited.\n", 0, 0);

//         kernel_mutex_release(mutex_id);

//         kernel_yield();
//     }

//     return 0;
// }
=======
>>>>>>> 347ac9667be18988db514b319f02a536414669b6
