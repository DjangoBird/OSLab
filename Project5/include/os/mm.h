/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include "os/list.h"
#include "os/sched.h"
#include <type.h>
#include <pgtable.h>

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK_M 0xffffffc052000000
#define INIT_KERNEL_STACK_S 0xffffffc052001000
#define FREEMEM_KERNEL (INIT_KERNEL_STACK_S + PAGE_SIZE)
#define FREEMEM_USER USER_STACK_ADDR

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
extern ptr_t allocUSERPage(int numPage);
// TODO [P4-task1] */
void freePage(ptr_t baseAddr);

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR 0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

// TODO [P4-task1] */
extern void* kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
void map_page_helper(uintptr_t va, uintptr_t pa, uintptr_t pgdir);
void freePage(ptr_t baseAddr);
extern void free_all_pages(pcb_t* pcb);


//TODO [P4-task3]: swap
#define USER_PAGE_MAX_NUM 32768  //free池子大小
#define KERNEL_PAGE_MAX_NUM 8//128  //最多2MB
extern int page_num;
extern int pgdir_id;
extern uintptr_t do_uva2pa(uintptr_t uva);
extern uint64_t image_end_sec;

typedef struct{
    list_node_t lnode;
    uint64_t uva;
    uint64_t pa;
    int disk_sector;
    int pgdir_id;
}alloc_info_t;

extern alloc_info_t alloc_info[USER_PAGE_MAX_NUM];
extern list_head in_mem_list;
extern list_head swap_out_list;
extern list_head free_list;
extern uintptr_t alloc_limit_page_helper(uintptr_t va, uintptr_t pgdir);
extern void init_uva_alloc();
extern alloc_info_t* swapPage();
extern ptr_t uva_allocPage(int numPage, uintptr_t uva);

static inline int get_id_from_pgdir(uintptr_t pgdir){
    for(int i = 0; i < NUM_MAX_TASK; i++){
        if(pgdir == pcb[i].pgdir)
            return i;
    }
    return -1;
}

static inline alloc_info_t* lnode2info(list_node_t* lnode){
    return (alloc_info_t *)lnode;
}

// TODO [P4-task4]: free
extern size_t get_free_memory();

// TODO [P4-task5]
#define MAX_PIPES 10    //10个管道
#define PIPE_SIZE 4096  //16MB管道
typedef struct
{
    uintptr_t data;
    int is_swap;
}pipe_page_t;

typedef struct{
    int valid;
    char name[32];

    pipe_page_t buffer[PIPE_SIZE];
    int head;
    int tail;
    int count;

    list_head wait_send;
    list_head wait_recv;
}page_pipe_t;

extern page_pipe_t pipes[MAX_PIPES];

#define PTE_V_MASK 0x1

static inline void init_list_head(list_head *list) {
    list->next = list;
    list->prev = list;
}

extern int pipe_open(const char *name);
extern long pipe_give_pages(int pipe_idx, void *src, size_t length);
extern long pipe_take_pages(int pipe_idx, void *dst, size_t length);

#endif /* MM_H */
