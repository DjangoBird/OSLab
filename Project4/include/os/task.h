#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x57000000
#define TASK_MAXNUM      32
#define TASK_SIZE        0x10000
#define TASK_INFO_MEM    0x52500000
#define BATCH_MAX_SIZE_FROM_CONFIG (4 * 512)
#define BATCH_BUF_BASE 0x55000000
#define MAX_LINE_LEN 128
#define BATCH_MAX_SIZE (4 * SECTOR_SIZE)

#define USER_ENTRYPOINT  0x10000

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    char task_name[16];
    int start_addr;
    int block_nums;
    int task_size;
    int p_memsz;
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];
extern int tasknum;

#endif