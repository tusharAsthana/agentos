/*
 * AgentOS HAC — Physical Memory Allocator
 *
 * Simple bump allocator for HAC internal use at EL2.
 * HAC gets a 64 MB private region (0x820000000 + code_size .. 0x824000000).
 * Anything above that is managed by the Linux kernel at EL1.
 */

#include "hac.h"

/* Private region for HAC allocations (after HAC binary itself) */
#define HAC_HEAP_BASE   0x821000000ULL   /* 1 GB offset into DRAM */
#define HAC_HEAP_SIZE   0x003000000ULL   /* 48 MB heap */

static uintptr_t heap_ptr  = HAC_HEAP_BASE;
static uintptr_t heap_end  = HAC_HEAP_BASE + HAC_HEAP_SIZE;

void mm_init(void)
{
    heap_ptr = HAC_HEAP_BASE;
    heap_end = HAC_HEAP_BASE + HAC_HEAP_SIZE;
    hac_uart_puts("[MM] Bump allocator ready: 48 MB\r\n");
}

void *mm_alloc(size_t size, size_t align)
{
    if (align == 0) align = 8;
    uintptr_t ptr = ALIGN_UP(heap_ptr, align);
    if (ptr + size > heap_end) {
        hac_uart_puts("[MM] FATAL: out of memory\r\n");
        while (1) asm volatile("wfi");
    }
    heap_ptr = ptr + size;
    /* Zero-fill */
    uint8_t *p = (uint8_t *)(uintptr_t)ptr;
    for (size_t i = 0; i < size; i++) p[i] = 0;
    return (void *)(uintptr_t)ptr;
}

/* Bump allocator does not support free — add slab later if needed */
void mm_free(void *ptr __attribute__((unused))) {}

phys_t mm_virt_to_phys(void *vaddr)
{
    /* HAC runs with identity mapping — VA == PA */
    return (phys_t)(uintptr_t)vaddr;
}
