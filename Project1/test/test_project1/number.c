#include<kernel.h>
#define PIPE_LOC 0x54000000

char buf[16];
int main(){
    int *p =(int *) PIPE_LOC;
    int number;
    int tmp;
    int taskname_len = 0;
    bios_putstr("\n\rthe number is :");
    while (1) {
        while ((tmp = bios_getchar()) == -1);
        bios_putchar(tmp);
        
        if (taskname_len >= 15) {
            if (tmp != '\r' && tmp != '\n') {
                bios_putstr("Error: number too big!\n\r");   
                taskname_len = 0; // Reset task name length
                continue;
            }
        }
        if (tmp == '\r' || tmp == '\n') {
            bios_putchar('\n');
            break;
        } else if (tmp >= '0' && tmp <= '9') {
            if (taskname_len < 15) {
                buf[taskname_len++] = tmp;
            }
        } else {
            bios_putstr("Error: invalid character!\n\r");
            taskname_len = 0; // Reset task name length
            continue;
        }
    }
    buf[taskname_len] = '\0';

    number = my_atoi(buf);
    *p = number;
    return 0;
}