#include <kernel.h>
#define PIPE_LOC 0x54000000

char buf[32];
int main(){
    int number;
    int *p = (int *)PIPE_LOC;
    number = *p;
    number *= 3;
    *p = number;
    my_itoa(number, 10, 0, 0, buf, 0);
    bios_putstr("\n\rmul3=");
    bios_putstr(buf);
    return 0;
}