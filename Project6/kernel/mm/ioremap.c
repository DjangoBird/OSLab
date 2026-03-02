#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

void *ioremap(unsigned long phys_addr, unsigned long size)
{
    // TODO: [p5-task1] map one specific physical region to virtual address
    uintptr_t pa_start = phys_addr & ~(PAGE_SIZE - 1);
    uintptr_t offset   = phys_addr & (PAGE_SIZE - 1);

    uintptr_t total_size = (size + offset + PAGE_SIZE -1) & ~(PAGE_SIZE - 1);

    uintptr_t va_ret = io_base + offset;

    uintptr_t mapped = 0;

    while(mapped < total_size){
        map_page_helper(io_base, pa_start + mapped, pa2kva(PGDIR_PA));
        io_base += PAGE_SIZE;
        mapped  += PAGE_SIZE; 
    }
    local_flush_tlb_all();

    return (void *)va_ret;

}

//似乎没用?
void iounmap(void *io_addr)
{
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
}
