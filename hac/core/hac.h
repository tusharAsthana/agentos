/*
 * AgentOS HAC — Shared Types and Macros
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------------------------------------------------------------
 * Compiler helpers
 * --------------------------------------------------------------- */
#define MMIO_READ32(addr)        (*((volatile uint32_t *)(uintptr_t)(addr)))
#define MMIO_WRITE32(addr, val)  (*((volatile uint32_t *)(uintptr_t)(addr)) = (uint32_t)(val))
#define MMIO_READ64(addr)        (*((volatile uint64_t *)(uintptr_t)(addr)))
#define MMIO_WRITE64(addr, val)  (*((volatile uint64_t *)(uintptr_t)(addr)) = (uint64_t)(val))

#define FIELD(val, hi, lo)       (((val) >> (lo)) & ((1ULL << ((hi)-(lo)+1)) - 1))
#define BIT(n)                   (1ULL << (n))
#define ALIGN_UP(x, a)           (((x) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(x)            (sizeof(x) / sizeof((x)[0]))

#define HAC_LOGI(msg) hac_uart_puts("[HAC] " msg "\r\n")

/* ---------------------------------------------------------------
 * Physical address type
 * --------------------------------------------------------------- */
typedef uint64_t phys_t;
typedef uint64_t virt_t;

/* ---------------------------------------------------------------
 * Memory allocator
 * --------------------------------------------------------------- */
void   mm_init(void);
void  *mm_alloc(size_t size, size_t align);
void   mm_free(void *ptr);
phys_t mm_virt_to_phys(void *vaddr);

/* ---------------------------------------------------------------
 * UART
 * --------------------------------------------------------------- */
void uart_early_init(void);
void hac_uart_putc(char c);
void hac_uart_puts(const char *s);
void hac_uart_puthex(uint64_t val);

/* ---------------------------------------------------------------
 * SMMU
 * --------------------------------------------------------------- */
void smmu_init(void);
void smmu_map_stream(uint32_t stream_id, phys_t pa, size_t size);

/* ---------------------------------------------------------------
 * Stage-2 page table
 * --------------------------------------------------------------- */
void hac_s2_pgtable_init(void);
void hac_s2_enable(void);

/* ---------------------------------------------------------------
 * Panic / debug
 * --------------------------------------------------------------- */
void hac_panic_sync(uint64_t esr, uint64_t far);
void hac_irq_handler(void);
void hac_fiq_handler(void);
void hac_serror_handler(void);
