#include "os/list.h"
#include "pgtable.h"
#include <os/mm.h>
#include <os/string.h>
#include <assert.h>
#include <os/sched.h>
#include <os/kernel.h>
#include <os/smp.h>
#include <os/task.h>
// #include <os/lock.h>


//SWAP管理
#define SECTORS_PER_PAGE (PAGE_SIZE/SECTOR_SIZE)
#define MAX_SWAP_PAGES 16384 //64MB
static uint8_t swap_bitmap[MAX_SWAP_PAGES/8];
static uint32_t swap_init_sector = 0;
#define SWAP_BITMAP_IDX(n) (n / 8)
#define SWAP_BITMAP_OFFSET(n) (n % 8)

uint32_t alloc_swap_sector() {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        // 寻找为 0 的位
        if (!(swap_bitmap[SWAP_BITMAP_IDX(i)] & (1 << SWAP_BITMAP_OFFSET(i)))) {
            // 置 1 (占用)
            swap_bitmap[SWAP_BITMAP_IDX(i)] |= (1 << SWAP_BITMAP_OFFSET(i));
            
            // 返回对应的物理扇区号
            return swap_init_sector + i * SECTORS_PER_PAGE;
        }
    }
    printk("Error: Swap Space (64MB) is Full!\n");
    return 0;
}

void free_swap_sector(uint32_t sector) {
    if (sector < swap_init_sector) return;

    int index = (sector - swap_init_sector) / SECTORS_PER_PAGE;
    
    if (index >= MAX_SWAP_PAGES) return;

    swap_bitmap[SWAP_BITMAP_IDX(index)] &= ~(1 << SWAP_BITMAP_OFFSET(index));
}
//////////////////////////////////////////////////////////////

#define KERNELMEM_START 0xffffffc050000000lu
#define KERNELMEM_END   0xffffffc060000000lu

alloc_info_t alloc_info[USER_PAGE_MAX_NUM];
page_pipe_t pipes[MAX_PIPES];

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t userMemCurr = FREEMEM_USER;

#define TOTAL_PAGES ((KERNELMEM_END - KERNELMEM_START) / PAGE_SIZE)
//数组
#define BITMAP(n) (n - KERNELMEM_START)/(8*PAGE_SIZE)
//bit
#define BITMAP_OFFSET(n) ((n - KERNELMEM_START)/(PAGE_SIZE))%8

static uint8_t page_bitmap[TOTAL_PAGES / 8];
static size_t current_free_pages = TOTAL_PAGES; 

void recycle_alloc_info(int pgdir_id);

void init_memory_manager() {
    bzero(page_bitmap, sizeof(page_bitmap));
    current_free_pages = TOTAL_PAGES; // 重置计数
    kernMemCurr = KERNELMEM_START;    // 确保指针复位
}

bool is_memory_full()
{
    return (current_free_pages == 0);
}

//VA
ptr_t allocPage(int numPage)
{
    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    while (1) {
        if (kernMemCurr >= KERNELMEM_END) {
            kernMemCurr = FREEMEM_KERNEL;
            if (is_memory_full()) {
                printk("Memory is full\n");
                return 0;
            }
        }
        
        ret = kernMemCurr;
        int i;
        int available = 1;
        
        for (i = 0; i < numPage; i++) {
            ptr_t addr = ret + i * PAGE_SIZE;
            if (addr >= KERNELMEM_END) {
                available = 0;
                break;
            }
            if (page_bitmap[BITMAP(addr)] & (1 << BITMAP_OFFSET(addr))) {
                available = 0;
                break;
            }
        }
        
        if (available) {
            for (i = 0; i < numPage; i++) {
                ptr_t addr = ret + i * PAGE_SIZE;
                page_bitmap[BITMAP(addr)] |= (1 << BITMAP_OFFSET(addr));
            }
            
            kernMemCurr = ret + numPage * PAGE_SIZE;
            
            if (current_free_pages >= numPage)
                current_free_pages -= numPage;
            else 
                current_free_pages = 0;
            return ret;
        }
        
        kernMemCurr += PAGE_SIZE;
    }
    return 0;
}

//分配用户虚拟地址
ptr_t allocUSERPage(int numPage){
    ptr_t ret = ROUND(userMemCurr, PAGE_SIZE);
    userMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif


void freePage(ptr_t baseAddr)
{
    // TODO [P4-task1] (design you 'freePage' here if you need):
    if(baseAddr == (ptr_t)NULL) return;

    // 检查地址是否有效
    if (baseAddr < KERNELMEM_START || baseAddr >= KERNELMEM_END) {
        return; // 地址无效
    }

    if (page_bitmap[BITMAP(baseAddr)] & (1 << (BITMAP_OFFSET(baseAddr)))) {
        page_bitmap[BITMAP(baseAddr)] &= ~(1 << (BITMAP_OFFSET(baseAddr)));
        current_free_pages++;
    }
}

void recycle_alloc_info(int pgdir_id){
    list_node_t *curr, *next;
    
    curr = in_mem_list.next;
    while(curr != &in_mem_list) {
        next = curr->next;
        alloc_info_t *info = lnode2info(curr);
        if (info->pgdir_id == pgdir_id) {
            delete_node(curr);
            if (info->pa != 0 && info->disk_sector == 0) {
                freePage(pa2kva(info->pa));
                
                if (page_num > 0) page_num--;
            }
            info->uva = 0;
            info->pa = 0;
            info->disk_sector = 0;
            info->pgdir_id = 0;
            Queue_push(curr, &free_list);
        }
        curr = next;
    }

    curr = swap_out_list.next;
    while(curr != &swap_out_list) {
        next = curr->next;
        alloc_info_t *info = lnode2info(curr);
        if (info->pgdir_id == pgdir_id) {
            delete_node(curr);
            if (info->disk_sector != 0) {
                free_swap_sector(info->disk_sector);
            }
            info->uva = 0;
            info->pa = 0;
            info->disk_sector = 0;
            info->pgdir_id = 0;
            Queue_push(curr, &free_list);
        }
        curr = next;
    }
}

void check_and_restore_page_num() {
    int count = 0;
    list_node_t *curr = in_mem_list.next;
    while (curr != &in_mem_list) {
        count++;
        curr = curr->next;
    }
    
    // 如果发现计数器与实际不符，强制修正
    if (page_num != count) {
        // printk("Warning: page_num fixed from %d to %d\n", page_num, count);
        page_num = count;
    }
}

void free_all_pages(pcb_t* pcb)
{
    PTE *pgd = (PTE*)pcb->pgdir;
    for(int i=0;i<512;i++)
    {
        if(pgd[i] == 0) continue;
        PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[i])));
        for(int j=0;j<512;j++)
        {
            if(pmd[j] == 0 || (pmd[j] & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) == 0) continue;
            PTE *pte = (uintptr_t *)pa2kva((get_pa(pmd[j])));
            for(int k=0;k<512;k++)
            {
                if(pte[k] == 0) continue;
                if(pte[k] & _PAGE_PRESENT){
                    freePage(pa2kva(get_pa(pte[k])));
                }
            }
            freePage(pa2kva(get_pa(pmd[j])));
        }
        freePage(pa2kva(get_pa(pgd[i])));
    }
    freePage(pcb->pgdir);
    freePage(pcb->kernel_sp - PAGE_SIZE);
    int id = get_id_from_pgdir(pcb->pgdir);
    recycle_alloc_info(id);

    check_and_restore_page_num();
}

//kernel malloc
void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
    return NULL;
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // TODO [P4-task1] share_pgtable:
    memcpy((void *)dest_pgdir, (void *)src_pgdir, PAGE_SIZE);
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    // TODO [P4-task1] alloc_page_helper:
    va &= VA_MASK;
    uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    PTE *pgd = (PTE*)pgdir;//根页表基址
    if(pgd[vpn2]==0){
        //分配一页物理内存作为二级页目录
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    //获取下一级页表的虚拟地址
    PTE *pmd = (uintptr_t *)pa2kva(get_pa(pgd[vpn2]));
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1],kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    if(pte[vpn0] == 0){
        ptr_t pa = kva2pa(allocPage(1));
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    }
    //_PAGE_GLOBAL不重要
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);  // | _PAGE_GLOBAL);
    return pa2kva(get_pa(pte[vpn0]));
}

void map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir)
{
    va &= VA_MASK;
    uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    
    PTE *pgd = (PTE*)pgdir;

    // Level 2
    if (pgd[vpn2] == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }

    // Level 1
    PTE *pmd = (PTE *)pa2kva(get_pa(pgd[vpn2]));
    if (pmd[vpn1] == 0) {
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }

    // Level 0
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    
    // 这里不 allocPage，而是直接使用传入的 pa
    if(pa == 0){
        //unmap
        pte[vpn0] = 0;
    }else{
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    
        // 设置权限 (User, RWX, AD)
        set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);

    }
}

//TODO [P4-task3]:FIFO-swap
LIST_HEAD(in_mem_list);
LIST_HEAD(swap_out_list);
LIST_HEAD(free_list);

uintptr_t do_uva2pa(uintptr_t uva){
    uint64_t cpu_id = get_current_cpu_id();
    return kva2pa(get_kva(uva, current_running[cpu_id]->pgdir));
}

int pgdir_id;
int page_num;
void init_uva_alloc(){
    page_num = 0;
    for(int i = 0; i < USER_PAGE_MAX_NUM; i++){
        Queue_push(&alloc_info[i].lnode, &free_list);
        alloc_info[i].uva = 0;
        alloc_info[i].pa  = 0;
        alloc_info[i].disk_sector = 0;
        alloc_info[i].pgdir_id = 0;
        
        bzero(swap_bitmap, sizeof(swap_bitmap));

        if (swap_init_sector == 0) {
            swap_init_sector = image_end_sec;
        }
    }
}

alloc_info_t* swapPage(){
    list_node_t* swap_lnode = in_mem_list.next;
    assert(swap_lnode!=&in_mem_list);

    delete_node(swap_lnode);
    Queue_push(swap_lnode, &swap_out_list);

    alloc_info_t* info = lnode2info(swap_lnode);

    printk("[FIFO-CHECK] Swap Out: vaddr=0x%lx, paddr=0x%lx\n", 
       info->uva, info->pa);

    asm volatile("fence rw, rw" ::: "memory");

    uint32_t target_sector = alloc_swap_sector();
    assert(target_sector != 0); 

    bios_sd_write(info->pa, PAGE_SIZE/SECTOR_SIZE, target_sector);

    asm volatile("fence rw, rw" ::: "memory");
    
    info->disk_sector = target_sector;

    // 将page置为不存在，下一次访问会触发例外
    map_page_helper(info->uva, 0, pcb[info->pgdir_id].pgdir);

    local_flush_tlb_all();

    clear_pgdir(pa2kva(info->pa));

    return info;
}

ptr_t uva_allocPage(int numPage, uintptr_t uva){
    uint64_t cpu_id = get_current_cpu_id();
    uintptr_t pa = 0;
    alloc_info_t* target_info = NULL;
    int is_swap_in = 0;

    for(list_node_t *lnode = swap_out_list.next; lnode != &swap_out_list; lnode = lnode->next){
        alloc_info_t *info = lnode2info(lnode);
        if(info->uva == uva && info->pgdir_id == pgdir_id){
            target_info = info;
            delete_node(&target_info->lnode);
            is_swap_in = 1;
            break;
        }
    }

    if(target_info == NULL){
        list_node_t* new_lnode = free_list.next;
        assert(new_lnode != &free_list);
        delete_node(new_lnode);

        target_info = lnode2info(new_lnode);
        target_info->uva = uva;
        target_info->pgdir_id = pgdir_id;
        target_info->disk_sector = 0;
    }

    if(page_num >= KERNEL_PAGE_MAX_NUM){
        alloc_info_t* victim = swapPage();
        pa = victim->pa;
        victim->pa = 0;
    }else{
        pa = kva2pa(allocPage(1));
        page_num++;
    }

    target_info->pa = pa;
    Queue_push(&target_info->lnode, &in_mem_list);
    map_page_helper(target_info->uva, target_info->pa, current_running[cpu_id]->pgdir);
    local_flush_tlb_all();

    if(is_swap_in){
        bios_sd_read(pa, PAGE_SIZE/SECTOR_SIZE, target_info->disk_sector);
        free_swap_sector(target_info->disk_sector);
        target_info->disk_sector = 0;
    }else {
        clear_pgdir(pa2kva(pa));
    }
    return pa2kva(pa);
}

uintptr_t alloc_limit_page_helper(uintptr_t va, uintptr_t pgdir){
    uint64_t cpu_id = get_current_cpu_id();
    pgdir_id = get_id_from_pgdir(current_running[cpu_id]->pgdir);
    va &= VA_MASK;
    uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    uint64_t vpn0 = (vpn2 << (PPN_BITS + PPN_BITS)) ^ (vpn1 << PPN_BITS) ^ (va >> NORMAL_PAGE_SHIFT);
    PTE *pgd = (PTE*)pgdir;
    if (pgd[vpn2] == 0) {
        set_pfn(&pgd[vpn2], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgd[vpn2])));
    }
    PTE *pmd = (uintptr_t *)pa2kva((get_pa(pgd[vpn2])));
    if(pmd[vpn1] == 0){
        set_pfn(&pmd[vpn1], kva2pa(allocPage(1)) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pmd[vpn1])));
    }
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1])); 
    if(pte[vpn0] == 0){
        ptr_t pa = kva2pa(uva_allocPage(1, (va>>NORMAL_PAGE_SHIFT)<<NORMAL_PAGE_SHIFT));
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    }
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER);
    return pa2kva(get_pa(pte[vpn0]));
}


// TODO [P4-task4]
size_t get_free_memory() {
    // Count used pages in bitmap
    size_t used_pages = 0;
    size_t bytes = (TOTAL_PAGES + 7) / 8;
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b = page_bitmap[i];
        // portable byte popcount
        b = (b & 0x55) + ((b >> 1) & 0x55);
        b = (b & 0x33) + ((b >> 2) & 0x33);
        b = (b & 0x0F) + ((b >> 4) & 0x0F);
        used_pages += b;
    }
    size_t free_pages = 0;
    if (used_pages >= TOTAL_PAGES) free_pages = 0;
    else free_pages = TOTAL_PAGES - used_pages;
    return free_pages * PAGE_SIZE;
}


// TODO [P4-task5]

alloc_info_t* find_alloc_info(uintptr_t uva, int pgdir_id) {
    list_node_t *curr;
    alloc_info_t *info;

    // 1. 搜索在内存中的列表
    curr = in_mem_list.next;
    while (curr != &in_mem_list) {
        info = lnode2info(curr);
        if (info->uva == uva && info->pgdir_id == pgdir_id) {
            return info;
        }
        curr = curr->next;
    }

    // 2. 搜索在磁盘交换区的列表
    curr = swap_out_list.next;
    while (curr != &swap_out_list) {
        info = lnode2info(curr);
        if (info->uva == uva && info->pgdir_id == pgdir_id) {
            return info;
        }
        curr = curr->next;
    }

    return NULL;
}

// 辅助：获取 alloc_info 在全局数组中的下标
int get_alloc_info_index(alloc_info_t *info) {
    if (!info) return -1;
    return (int)(info - alloc_info); // 指针相减得到下标
}


#ifndef PPN_MASK
#define PPN_MASK 0x0FFFFFFFFFFFFC00UL
#endif

// 内存屏障宏：确保读写操作对内存可见
#define SMP_MB() asm volatile("fence rw, rw" ::: "memory")



static void reset_pipe(page_pipe_t *pipe) {
    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
    init_list_head(&pipe->wait_send);
    init_list_head(&pipe->wait_recv);
}

void release_pipe_buffer(int pipe_idx) {
    page_pipe_t *pipe = &pipes[pipe_idx];
    
    while(pipe->count > 0) {
        pipe_page_t token = pipe->buffer[pipe->tail];
        pipe->tail = (pipe->tail + 1) % PIPE_SIZE;
        pipe->count--;

        if (token.is_swap) {
            int idx = (int)token.data;
            alloc_info_t *info = &alloc_info[idx];

            if(info->disk_sector == 0) {
                if(info->pa != 0) {
                    freePage(pa2kva(info->pa));
                }
            } else {
                free_swap_sector(info->disk_sector);
            }
            
            info->uva = 0;
            info->pa = 0;
            info->disk_sector = 0;
            info->pgdir_id = 0;

            info->lnode.next = &info->lnode;
            info->lnode.prev = &info->lnode;
            Queue_push(&info->lnode, &free_list);
        }
    }

    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
}

int pipe_open(const char *name) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].valid && strcmp(pipes[i].name, name) == 0) {
            if (pipes[i].wait_send.next == &pipes[i].wait_send && 
                pipes[i].wait_recv.next == &pipes[i].wait_recv &&
                pipes[i].count > 0) {
                release_pipe_buffer(i);
            }
            return i;
        }
    }

    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].valid) {
            pipes[i].valid = 1;
            strcpy(pipes[i].name, name);
            reset_pipe(&pipes[i]);
            return i;
        }
    }
    return -1;
}

long pipe_give_pages(int pipe_idx, void *src, size_t length) {
    uint64_t cpu_id = get_current_cpu_id();
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    if ((uintptr_t)src % PAGE_SIZE != 0 || length % PAGE_SIZE != 0) return -1;

    page_pipe_t *pipe = &pipes[pipe_idx];
    uintptr_t current_va = (uintptr_t)src;
    uintptr_t end_va = current_va + length;
    int current_pgdir_id = get_id_from_pgdir(current_running[cpu_id]->pgdir);
    int loop_counter = 0;

    while (current_va < end_va) {
        while (pipe->count >= PIPE_SIZE) {
            current_running[cpu_id]->status = TASK_BLOCKED;
            do_block((list_node_t *)&current_running[cpu_id]->list, &pipe->wait_send);
        }

        alloc_info_t *info = find_alloc_info(current_va, current_pgdir_id);
        uint64_t token_data;
        int is_valid_page = 0;

        if (info != NULL) {
            token_data = get_alloc_info_index(info);
            is_valid_page = 1;

            // 切断与当前进程的联系
            info->pgdir_id = 0; 
            
            // 从内存链表移除，进入“管道托管”状态
            delete_node(&info->lnode);
            info->lnode.next = &info->lnode;
            info->lnode.prev = &info->lnode;

            if (info->disk_sector == 0 && page_num > 0) {
                page_num--;
            }
        } else {
            uintptr_t src_pa = do_uva2pa(current_va);
            if (src_pa == 0) {
                return -1; 
            }

            list_node_t *new_node = free_list.next;
            assert(new_node != &free_list); 
            delete_node(new_node);
            alloc_info_t *new_info = lnode2info(new_node);

            new_info->uva = current_va;
            new_info->pa = src_pa;
            new_info->pgdir_id = 0;
            new_info->disk_sector = 0;
            
            token_data = get_alloc_info_index(new_info);
            is_valid_page = 1;
            // list_node_t *new_node = free_list.next;
            // assert(new_node != &free_list); 
            // delete_node(new_node);
            // alloc_info_t *new_info = lnode2info(new_node);

            // ptr_t kva = 0;
            // if (page_num >= KERNEL_PAGE_MAX_NUM) {
            //      alloc_info_t* victim = swapPage();
            //      kva = pa2kva(victim->pa);
            //      victim->pa = 0;
            // } else {
            //      kva = allocPage(1);
            //      page_num++;
            // }
            
            // if (kva == 0) {
            //      return -1; 
            // }

            // uintptr_t src_pa = do_uva2pa(current_va);
            // if (src_pa != 0) {
            //     memcpy((void*)kva, (void*)pa2kva(src_pa), PAGE_SIZE);
            // } else {
            //     bzero((void*)kva, PAGE_SIZE);
            // }

            // // 4. 登记造册，放入“管道托管”状态（pgdir_id=0）
            // new_info->uva = current_va;
            // new_info->pa = kva2pa(kva);
            // new_info->pgdir_id = 0;      // 管道暂时持有
            // new_info->disk_sector = 0;
            
            // // 5. 设置 token
            // token_data = get_alloc_info_index(new_info);
            // is_valid_page = 1;
        }
        
        SMP_MB();

        // 写入管道
        pipe->buffer[pipe->head].data = token_data;
        pipe->buffer[pipe->head].is_swap = is_valid_page;
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
        pipe->count++;
        
        if ((pipe->wait_recv.next != &pipe->wait_recv)) {
            do_unblock(pipe->wait_recv.next);
        }

        // 解除当前映射
        map_page_helper(current_va, 0, current_running[cpu_id]->pgdir);
        asm volatile("sfence.vma");

        current_va += PAGE_SIZE;
    }
    return length;
}

// -------------------------------------------------------------------------
// pipe_take_pages: 接收页面
// -------------------------------------------------------------------------
long pipe_take_pages(int pipe_idx, void *dst, size_t length) {
    uint64_t cpu_id = get_current_cpu_id();
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].valid) return -1;
    if ((uintptr_t)dst % PAGE_SIZE != 0 || length % PAGE_SIZE != 0) return -1;

    page_pipe_t *pipe = &pipes[pipe_idx];
    uintptr_t current_va = (uintptr_t)dst;
    uintptr_t end_va = current_va + length;
    int current_pgdir_id = get_id_from_pgdir(current_running[cpu_id]->pgdir);
    int loop_counter = 0;

    while (current_va < end_va) {
        // if (++loop_counter % 32 == 0) {
        //     current_running[cpu_id]->status = TASK_RUNNING;
        //     do_scheduler();
        // }

        while (pipe->count <= 0) {
            current_running[cpu_id]->status = TASK_BLOCKED;
            do_block((list_node_t *)&current_running[cpu_id]->list, &pipe->wait_recv);
        }

        pipe_page_t token = pipe->buffer[pipe->tail];
        pipe->tail = (pipe->tail + 1) % PIPE_SIZE;
        pipe->count--;

        if ((pipe->wait_send.next != &pipe->wait_send)) {
            do_unblock(pipe->wait_send.next);
        }

        // --- 清理接收地址原有的页面 ---
        alloc_info_t *old_info = find_alloc_info(current_va, current_pgdir_id);
        if (old_info != NULL) {
            map_page_helper(current_va, 0, current_running[cpu_id]->pgdir);
            delete_node(&old_info->lnode);
            
            if (old_info->disk_sector == 0) {
                if (page_num > 0) page_num--;
                if (old_info->pa != 0) freePage(pa2kva(old_info->pa));
            } else {
                free_swap_sector(old_info->disk_sector);
            }
            
            old_info->uva = 0;
            old_info->pa = 0;
            old_info->pgdir_id = 0;
            old_info->disk_sector = 0;
            Queue_push(&old_info->lnode, &free_list);
            asm volatile("sfence.vma");
        }

        // --- 接收新页面 ---
        if (token.is_swap == 0) {
            if (uva_allocPage(1, current_va) == 0) return -1;
        } 
        else {
            // Case A/B: 复用页面
            int idx = (int)token.data;
            alloc_info_t *info = &alloc_info[idx];

            info->pgdir_id = current_pgdir_id;
            info->uva = current_va;

            if (info->disk_sector == 0) {
                Queue_push(&info->lnode, &in_mem_list);
                map_page_helper(current_va, info->pa, current_running[cpu_id]->pgdir);

                page_num++;
            } else {
                Queue_push(&info->lnode, &swap_out_list);
                map_page_helper(current_va, 0, current_running[cpu_id]->pgdir);
            }
            asm volatile("sfence.vma");
        }

        SMP_MB();
        current_va += PAGE_SIZE;
    }
    return length;
}