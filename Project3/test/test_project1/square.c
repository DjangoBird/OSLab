#include <kernel.h>
#define PIPE_LOC 0x54000000
char buf[32];
int main()
{
    int number;
    int *p = (int *)PIPE_LOC;
    number = *p;
    number = number * number;
    *p = number;
    my_itoa(number, 10, 0, 0, buf, 0);
    bios_putstr("\n\rsquare=");
    bios_putstr(buf);
    bios_putstr("\n\r");
    return 0;
}