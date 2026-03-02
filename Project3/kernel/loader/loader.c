#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define BUF_FOR_APP 0x59000000

// uint64_t load_task_img(int taskid)
// {
//     /*
//      * TODO:
//      * 1. [p1-task3] load task from image via task id, and return its entrypoint
//      * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
//      */
//     //task3
//     uint64_t entry_addr;
//     int start_sector = 1 + taskid * 15; //1 for kernel, 15 for each task

//     char info[] = "Loading task _ ...\n\r";
//     for(int i=0;i<strlen(info);i++){
//         if(info[i]!='_') bios_putchar(info[i]);
//         else bios_putchar(taskid +'0');
//     }

//     entry_addr = TASK_MEM_BASE + TASK_SIZE * (taskid - 1);  //TASK_MEM_BASE是APP1的起始地址
//     bios_sd_read(entry_addr, 15, start_sector);
//     //bios_sd_read(unsigned mem_address, unsigned num_of_blocks, unsigned block_id)

//     return entry_addr;
// }

uint64_t load_task_img(char *taskname)
{
    int entry_addr;
    int start_sector;
    int i;
    for(i=0;i<TASK_MAXNUM;i++){
        if(strcmp(tasks[i].task_name, taskname)==0){
            entry_addr = TASK_MEM_BASE + TASK_SIZE * i;  //TASK_MEM_BASE是APP1的起始地址
            start_sector = tasks[i].start_addr / 512;//起始扇区，向下取整
            bios_sd_read(BUF_FOR_APP, tasks[i].block_nums, start_sector);//扇区数自适应
            memcpy((uint8_t *)(uint64_t)(entry_addr), (uint8_t *)(uint64_t)(BUF_FOR_APP + (tasks[i].start_addr - start_sector*512)), tasks[i].block_nums * 512); 
            return entry_addr;
        }
    }
    //没找到
    char info[] = "Task _ not found!\n\r";
    for(i=0;i<strlen(info);i++){
        if(info[i]!='_'){
            bios_putchar(info[i]);
        }else{
            int j;
            for(j=0;taskname[j]!='\0';j++){
                bios_putchar(taskname[j]);
            }
        }
    }
    return 0;

}


// void batch_write(int batch_info_loc, int batch_info_size)
// {
//     uint8_t line_buf[MAX_LINE_LEN]; // 用于存储当前行输入
//     char *batch_buf = (char *)BATCH_BUF_BASE;
//     int current_len = 0;
//     int len = 0;
//     int tmp;

//     bios_putstr("--- Entering batch write mode (End with empty line) ---\n\r");


//     while (1) {
//         bios_putstr("> ");
        
//         len = 0; 
        
//         while (1) {
//             while ((tmp = bios_getchar()) == -1); 
//             bios_putchar(tmp); // 回显
            
//             if (tmp == '\r' || tmp == '\n') {
//                 bios_putchar('\n');
//                 break;
//             } 
            
//             if (len >= MAX_LINE_LEN - 1) {
//                 continue; 
//             }
            
//             line_buf[len] = (uint8_t)tmp;
//             len++;
//         }

//         if (len == 0) {
//             break; 
//         }
        
//         if (current_len + len + 1 > batch_info_size) {
//             bios_putstr("Error: Batch file capacity exceeded. Write aborted.\n\r");
//             return;
//         }

//         memcpy((uint8_t *)(batch_buf + current_len), line_buf, len);
//         current_len += len;
//         batch_buf[current_len++] = '\n'; // 使用 \n 作为命令分隔符,因为'\0'是结束符

//     } 
        
    
//     //清零缓冲区剩余空间
//     if (current_len < batch_info_size) {
//         memset((void *)( (unsigned long)BATCH_BUF_BASE + current_len ), 0, batch_info_size - current_len);
//     }

//     // 计算 SD 卡写入参数
//     int start_sector = batch_info_loc / SECTOR_SIZE;
//     int blocknums = NBYTES2SEC(batch_info_loc + batch_info_size) - start_sector; 
    
//     int offset_in_first_sector = batch_info_loc % SECTOR_SIZE;

//     unsigned bytes_written_from_buf = 0;

//     if(offset_in_first_sector != 0){
//         // 如果 batch_info_loc 不是扇区对齐的，我们需要先读出第一个扇区的内容，更新后再写回去
//         uint8_t temp_sector[SECTOR_SIZE];
//         bios_sd_read((unsigned)temp_sector, 1, (unsigned)start_sector);

//         int remaining_space_in_sector = SECTOR_SIZE - offset_in_first_sector;

//         int total_bytes_to_write = batch_info_size < remaining_space_in_sector ? batch_info_size : remaining_space_in_sector;

//         memcpy(temp_sector + offset_in_first_sector, (uint8_t *)BATCH_BUF_BASE, total_bytes_to_write);
//         bios_sd_write((unsigned)temp_sector, 1, (unsigned)start_sector);
//         // 减去已经写入的部分
//         blocknums -= 1;
//         start_sector += 1;
//         bytes_written_from_buf += total_bytes_to_write;
//     }

//     if(blocknums > 0){
//         bios_putstr("Synchronizing batch commands to disk...\n\r");
//         bios_sd_write((unsigned)BATCH_BUF_BASE + bytes_written_from_buf, (unsigned)blocknums, (unsigned)start_sector);
//     }else{
//         bios_putstr("Synchronizing batch commands to disk...\n\r");
//     }
//         bios_putstr("Batch write complete.\n\r");
// }


// void batch_run(int batch_info_loc, int batch_info_size)
// {
//     bios_putstr("--- Starting batch run ---\n\r");
    
//     if (batch_info_loc == 0 || batch_info_size == 0) {
//         bios_putstr("Error: Batch location is invalid.\n\r");
//         return;
//     }

//     //SD 卡加载参数计算
//     int start_sector = batch_info_loc / SECTOR_SIZE;
//     int blocknums = NBYTES2SEC(batch_info_loc + batch_info_size) - start_sector; 
    
//     int offset_in_first_sector = batch_info_loc % SECTOR_SIZE;

//     //从 SD 卡加载最新命令内容
//     bios_putstr("Loading batch commands from disk...\n\r");
//     bios_sd_read((unsigned)BATCH_BUF_BASE, (unsigned)blocknums, (unsigned)start_sector);
    
//     uint8_t *batch_buf = (uint8_t *)BATCH_BUF_BASE;
    
//     if(offset_in_first_sector != 0){
//         // 如果 batch_info_loc 不是扇区对齐的，我们需要调整指针到正确的偏移位置
//         batch_buf += offset_in_first_sector;
//     }

//     if (batch_buf[0] == '\0') {
//         bios_putstr("Batch file is empty or corrupted.\n\r");
//         return;
//     }

//     uint8_t *ptr = batch_buf;
//     uint8_t *end = batch_buf + batch_info_size; 
    
//     while (ptr < end && *ptr != '\0') {
//         uint8_t *line_end = ptr;
        
//         while (*line_end != '\n' && line_end < end && *line_end != '\0') {
//             line_end++;
//         }
        
//         int cmd_len = line_end - ptr;
        
//         if (cmd_len > 0) {
//             uint8_t temp_char = *line_end;
//             *line_end = '\0'; // 临时终止字符串
            

//             uint64_t entry_addr;
//             void (*entry) (void); 

//             bios_putstr("\n\r[Batch] Run: ");

//             bios_putstr((char*)ptr); 
            

//             entry_addr = load_task_img((char*)ptr);
            
//             *line_end = temp_char; // 恢复分隔符

//             if (entry_addr != 0) {
//                 entry = (void*) entry_addr;
//                 entry();
//                 bios_putstr("\n\rCommand finished.\n\r");
//             } else {
//                 bios_putstr("\n\rError: Task not found or failed to load.\n\r");
//             }
//         }
        
//         if (*line_end == '\0') break; 
//         ptr = line_end + 1;
//     }

//     bios_putstr("--- Batch run finished ---\n\r");
// }