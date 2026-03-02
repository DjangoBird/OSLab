#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50


int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
}

static void init_task_info(int app_info_loc , int app_info_size)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int start_sector = app_info_loc / 512;
    int blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sector;
    //不用+1，NBYTES2SEC向上取整了
    //createimage中APP_INFO_LOC包含了app_info_size和app_info_loc
    int task_info_addr = TASK_INFO_MEM;
    //app info load to memory:0x52500000
    bios_sd_read(task_info_addr, blocknums, start_sector);
    int start_addr = TASK_INFO_MEM + app_info_loc - start_sector * 512;
    uint64_t *p = (uint64_t *)start_addr;
    memcpy((void *)tasks, (void *)p, app_info_size);
}

static void batch_run(int batch_info_loc, int batch_info_size);
static void batch_write(int batch_info_loc, int batch_info_size);

static void init_batch_info(int batch_info_loc , int batch_info_size)
{
    int start_sector = batch_info_loc / 512;
    int blocknums = NBYTES2SEC(batch_info_loc + batch_info_size) - start_sector;
    int batch_info_addr = BATCH_BUF_BASE;
    bios_sd_read(batch_info_addr, blocknums, start_sector);
    //初始化batch info到内存
    int start_addr = BATCH_BUF_BASE + batch_info_loc - start_sector * 512;
    void *src_addr = (void *)start_addr;
    
    // 目标地址：由于我们使用 BATCH_BUF_BASE 作为 SD 读取的地址，
    // 这里我们只是在 BATCH_BUF_BASE 内部进行整理，确保内容从 BATCH_BUF_BASE 开始。
    void *dest_addr = (void *)BATCH_BUF_BASE; 

    // 将 batch 文件内容（从偏移处开始）拷贝到 BATCH_BUF_BASE 的头部
    memcpy(dest_addr, src_addr, batch_info_size); 
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(int app_info_loc, int app_info_size, int batch_info_loc, int batch_info_size)
{
    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info(app_info_loc, app_info_size);

    // Init batch information
    init_batch_info(batch_info_loc, batch_info_size);

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";

    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    
    bios_putstr("\n\rTasks:\n\r");
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].task_name[0] != '\0') {
            bios_putstr(" - ");
            bios_putstr(tasks[i].task_name);
            bios_putstr("\n\r");
        }
    }
    bios_putstr("\n\r");

    //task2
    // int ch;
    // while(1){
    //     while((ch=bios_getchar())==-1){};
    //     bios_putchar(ch);
    // }

    //task3
    // int taskid;
    // uint64_t entry_addr;
    // void (*entry) (void);//函数指针
    // while(1){
    //     while((taskid=bios_getchar())==-1){};
    //     bios_putchar(taskid);
    //     taskid -= '0';
    //     if(taskid>=0 && taskid<=TASK_MAXNUM){
    //         bios_putchar('\n');
    //         entry_addr = load_task_img(taskid);
    //         entry = (void*) entry_addr;
    //         entry();//跳转运行对应地址的程序
    //     }
    // }

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.
    //task4
    char taskname[TASK_MAXNUM] = {0};
    int taskname_len = 0;
    int tmp;
    uint64_t entry_addr;
    void (*entry) (void);

    while (1) {
        while ((tmp = bios_getchar()) == -1);
        bios_putchar(tmp);
        
        if (taskname_len >= TASK_MAXNUM - 1) {
            if (tmp != '\r' && tmp != '\n') {
                continue;
            }
        }
        
        if (tmp == '\r' || tmp == '\n') {
            bios_putchar('\n');
            
            if (taskname_len > 0) {
                taskname[taskname_len] = '\0'; 
                
                if (strcmp(taskname,"batch_run")==0){
                batch_run(batch_info_loc, batch_info_size);
                }else if (strcmp(taskname,"batch_write")==0){
                    batch_write(batch_info_loc, batch_info_size);
                }else {
                    entry_addr = load_task_img(taskname);
                    if (entry_addr != 0) {
                        entry = (void*) entry_addr;
                        entry();
                    }
                }
            }
            
            taskname_len = 0;
            taskname[0] = '\0'; 
            
        } else {
            taskname[taskname_len] = (char)tmp;
            taskname_len++;
        }
    }


    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        asm volatile("wfi");
    }

    return 0;
}
