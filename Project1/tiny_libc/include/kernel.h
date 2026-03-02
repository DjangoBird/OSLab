#ifndef __INCLUDE_KERNEL_H__
#define __INCLUDE_KERNEL_H__

#define KERNEL_JMPTAB_BASE 0x51ffff00
typedef enum {
    CONSOLE_PUTSTR,
    CONSOLE_PUTCHAR,
    CONSOLE_GETCHAR,
    NUM_ENTRIES
} jmptab_idx_t;

static inline long call_jmptab(long which, long arg0, long arg1, long arg2, long arg3, long arg4)
{
    unsigned long val = \
        *(unsigned long *)(KERNEL_JMPTAB_BASE + sizeof(unsigned long) * which);
    long (*func)(long, long, long, long, long) = (long (*)(long, long, long, long, long))val;

    return func(arg0, arg1, arg2, arg3, arg4);
}

static inline void bios_putstr(char *str)
{
    call_jmptab(CONSOLE_PUTSTR, (long)str, 0, 0, 0, 0);
}

static inline void bios_putchar(int ch)
{
    call_jmptab(CONSOLE_PUTCHAR, (long)ch, 0, 0, 0, 0);
}

static inline int bios_getchar(void)
{
    return call_jmptab(CONSOLE_GETCHAR, 0, 0, 0, 0, 0);
}


#define TRUE 1
#define FALSE 0

// 你的 my_itoa 函数，简化并包含反转逻辑
unsigned int my_itoa(
    long value, unsigned int radix, unsigned int uppercase,
    unsigned int unsig, char *buffer, unsigned int zero_pad)
{
    char *pbuffer = buffer;
    int negative = 0;
    unsigned int i, len;
    unsigned long uvalue; 

    if (value < 0 && unsig == FALSE) {
        negative = TRUE;
        uvalue = (unsigned long)-value; 
    } else {
        uvalue = (unsigned long)value;
    }
    
    do {
        unsigned int digit = uvalue % radix;
        *(pbuffer++) =
            (digit < 10 ? '0' + digit :
                             (uppercase ? 'A' : 'a') + digit - 10);
        uvalue /= radix;
    } while (uvalue > 0);

    for (i = (pbuffer - buffer); i < zero_pad; i++)
        *(pbuffer++) = '0';

    if (negative) *(pbuffer++) = '-';

    *(pbuffer) = '\0';

    len = (pbuffer - buffer);
    int start = 0;
    int end = len - 1;
    char temp;
    
    while (start < end) {
        temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
    return len;
}

int my_atoi(const char *str) {
    int result = 0;
    int sign = 1;

    while (*str == ' ') {
        str++;
    }

    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        int digit = *str - '0';

        result = result * 10 + digit; 
        
        str++;
    }

    return sign * result;
}

#endif