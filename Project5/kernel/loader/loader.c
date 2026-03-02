#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <type.h>
#include <pgtable.h>

#define BUF_FOR_APP_KVA  pa2kva(0x59000000)

uint64_t load_task_img(char *taskname, uintptr_t pgdir)
{
    int i;
    int start_sector;
    uint64_t user_va;
    
    for(i = 0; i < TASK_MAXNUM; i++) {
        if (strcmp(tasks[i].task_name, taskname) == 0) {
            start_sector = tasks[i].start_addr / SECTOR_SIZE;
            bios_sd_read(kva2pa(BUF_FOR_APP_KVA), tasks[i].block_nums, start_sector);

            uint64_t offset_in_buffer = tasks[i].start_addr % SECTOR_SIZE;
            uint8_t *src_base = (uint8_t *)(BUF_FOR_APP_KVA + offset_in_buffer);

            uint64_t current_offset = 0;
            uint64_t remain_size = tasks[i].p_memsz;

            //4KB每页切割
            for (user_va = USER_ENTRYPOINT; 
                 current_offset < tasks[i].p_memsz; 
                 user_va += PAGE_SIZE) 
            {
                uintptr_t page_kva = alloc_page_helper(user_va, pgdir);
                
                uint64_t copy_size = (remain_size > PAGE_SIZE) ? PAGE_SIZE : remain_size;
                memcpy((void *)page_kva, 
                       (void *)(src_base + current_offset), 
                       copy_size);
                if (copy_size < PAGE_SIZE) {
                    memset((void *)(page_kva + copy_size), 0, PAGE_SIZE - copy_size);
                }
                current_offset += copy_size;
                remain_size -= copy_size;
            }
            asm volatile("fence.i");

            return USER_ENTRYPOINT;
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