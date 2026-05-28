/*
 * AgentOS HAC — EL2 Main Entry (C)
 *
 * Called from _el2_entry_trampoline after stack setup.
 * Performs full HAC initialization then boots the Linux kernel.
 */

#include "hac.h"
#include "../mmio/a11_mmio.h"
#include "../ipc/ipc.h"
#include "../primitives/primitives.h"
#include "../security/caps.h"

/* ---------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------- */
extern void hac_install_vectors(void);
void mm_init(void);
void smmu_init(void);
void uart_early_init(void);
void hac_uart_puts(const char *s);
void hac_boot_kernel(uintptr_t kernel_pa, uintptr_t dtb_pa, uintptr_t initrd_pa);

/* ---------------------------------------------------------------
 * Physical memory layout for A11 iPhone 8 / X
 *
 * Based on Apple Device Tree (t8010 / t8015) – these regions are
 * well-documented in PongoOS and postmarketOS device trees.
 *
 * DRAM base:  0x800000000  (34-bit PA on A11)
 * DRAM size:  0xC0000000   (3 GB, iPhone 8/X)
 *
 * HAC binary is loaded by PongoOS into DRAM at ~0x820000000.
 * Linux kernel is placed at 0x800080000 (standard arm64 Image offset).
 * Device Tree Blob (DTB) at 0x84F000000.
 * initrd at 0x84F100000.
 * --------------------------------------------------------------- */
#define A11_DRAM_BASE           0x800000000ULL
#define A11_DRAM_SIZE           0x0C0000000ULL  /* 3 GB */
#define HAC_LOAD_ADDR           0x820000000ULL  /* PongoOS loads HAC here */
#define HAC_RESERVED_SIZE       0x04000000ULL   /* 64 MB for HAC code/data/stack */
#define LINUX_KERNEL_PA         0x800080000ULL  /* arm64 Image load address */
#define LINUX_DTB_PA            0x84F000000ULL
#define LINUX_INITRD_PA         0x84F100000ULL
#define HAC_IPC_SHMEM_PA        0x84E000000ULL  /* 4 MB shared memory for EL2↔EL1 IPC */
#define HAC_IPC_SHMEM_SIZE      0x00400000ULL

/* ---------------------------------------------------------------
 * hac_main — called from assembly trampoline
 * --------------------------------------------------------------- */
void hac_main(void)
{
    /* 1. Install EL2 exception vectors (defined in entry.S) */
    hac_install_vectors();

    /* 2. Initialize UART for early debug output */
    uart_early_init();
    hac_uart_puts("\r\n[HAC] AgentOS Hardware Agent Controller v0.1\r\n");
    hac_uart_puts("[HAC] EL2 active, VBAR_EL2 installed\r\n");

    /* 3. Initialize physical memory allocator */
    hac_uart_puts("[HAC] Initializing memory allocator...\r\n");
    mm_init();

    /* 4. Initialize SMMU: lock down all DMA masters, grant EL2 bypass */
    hac_uart_puts("[HAC] Configuring SMMU...\r\n");
    smmu_init();

    /* 5. Initialize HAC IPC shared memory region */
    hac_uart_puts("[HAC] Setting up EL2<->EL1 IPC ring buffers...\r\n");
    ipc_init((volatile void *)HAC_IPC_SHMEM_PA, HAC_IPC_SHMEM_SIZE);

    /* 6. Initialize hardware primitives (register MMIO maps) */
    hac_uart_puts("[HAC] Registering hardware primitives...\r\n");
    primitives_init();

    /* 7. Initialize capability model */
    hac_uart_puts("[HAC] Initializing capability model...\r\n");
    caps_init();

    /* 8. Initialize LLM inference engine (loads model from NAND, async) */
    hac_uart_puts("[HAC] Starting inference engine (async)...\r\n");
    /* inference_init_async() — called after kernel is running to avoid blocking boot */

    /* 9. Set up stage-2 page tables: map DRAM, protect MMIO from EL1 */
    hac_uart_puts("[HAC] Building stage-2 page tables...\r\n");
    hac_s2_pgtable_init();

    /* 10. Enable stage-2 translation */
    hac_uart_puts("[HAC] Enabling stage-2 translation...\r\n");
    hac_s2_enable();

    /* 11. Hand off to Linux kernel at EL1 */
    hac_uart_puts("[HAC] Booting Linux kernel at EL1...\r\n");
    hac_boot_kernel(LINUX_KERNEL_PA, LINUX_DTB_PA, LINUX_INITRD_PA);

    /* Should never reach here */
    hac_uart_puts("[HAC] FATAL: kernel returned to EL2!\r\n");
    while (1) {
        asm volatile("wfi");
    }
}

/* ---------------------------------------------------------------
 * Boot Linux kernel at EL1
 * ARM64 kernel calling convention:
 *   x0 = physical address of DTB
 *   x1 = 0
 *   x2 = 0
 *   x3 = 0
 *   PC = kernel entry point (physical)
 * --------------------------------------------------------------- */
void hac_boot_kernel(uintptr_t kernel_pa, uintptr_t dtb_pa,
                     uintptr_t initrd_pa __attribute__((unused)))
{
    /* Configure SPSR_EL2 for EL1h entry with interrupts masked */
    asm volatile(
        "mov  x4, #0x3C5         \n\t"  /* EL1h (0b0101), DAIF masked */
        "msr  SPSR_EL2, x4       \n\t"
        "msr  ELR_EL2, %0        \n\t"  /* kernel entry */
        "mov  x0, %1             \n\t"  /* DTB physical address → x0 */
        "mov  x1, xzr            \n\t"
        "mov  x2, xzr            \n\t"
        "mov  x3, xzr            \n\t"
        "eret                    \n\t"
        :
        : "r"(kernel_pa), "r"(dtb_pa)
        : "x4", "x0", "x1", "x2", "x3", "memory"
    );
}

/* ---------------------------------------------------------------
 * Stage-2 page table helpers (minimal identity map implementation)
 * Full implementation would use 4KB granule, 3-level tables.
 * --------------------------------------------------------------- */

/* Stage-2 page table root (allocated in HAC BSS) */
static uint64_t __attribute__((aligned(4096))) s2_pgd[512];
static uint64_t __attribute__((aligned(4096))) s2_pud[512];
static uint64_t __attribute__((aligned(4096))) s2_pmd_dram[512];

#define S2_ATTR_NORMAL  (0x3 << 0)   /* Valid, block descriptor */
#define S2_ATTR_MEM     (0xF << 2)   /* MemAttr: Normal WB */
#define S2_ATTR_RDWR    (0x3 << 6)   /* S2AP: R/W */
#define S2_ATTR_INNER   (0x3 << 8)   /* SH: Inner shareable */
#define S2_ATTR_AF      (1ULL << 10) /* Access flag */

void hac_s2_pgtable_init(void)
{
    /* Identity-map all DRAM as Normal memory accessible by EL1 */
    /* PGD[4] → PUD → PMD covering 0x800000000..0x8BFFFFFFF */

    uintptr_t pgd_idx = (A11_DRAM_BASE >> 39) & 0x1FF;  /* VA[47:39] */
    uintptr_t pud_idx = (A11_DRAM_BASE >> 30) & 0x1FF;  /* VA[38:30] */

    s2_pgd[pgd_idx] = (uint64_t)(uintptr_t)s2_pud | 0x3; /* Table descriptor */
    s2_pud[pud_idx] = (uint64_t)(uintptr_t)s2_pmd_dram | 0x3;

    /* Map 3 GB DRAM in 2MB blocks */
    for (unsigned i = 0; i < (A11_DRAM_SIZE >> 21); i++) {
        uint64_t pa = A11_DRAM_BASE + ((uint64_t)i << 21);
        s2_pmd_dram[i] = pa |
            S2_ATTR_NORMAL | S2_ATTR_MEM | S2_ATTR_RDWR |
            S2_ATTR_INNER  | S2_ATTR_AF;
    }

    /* Write VTTBR_EL2 with root table physical address and VMID=0 */
    asm volatile(
        "msr  VTTBR_EL2, %0  \n\t"
        "isb                  \n\t"
        :
        : "r"((uint64_t)(uintptr_t)s2_pgd)
        : "memory"
    );

    /* Set VTCR_EL2: 4KB granule, 44-bit IPA, 3-level lookup */
    uint64_t vtcr = (0x2C) |   /* T0SZ=44, SL0=1 (level 1 start) */
                    (0x1 << 6) | /* IRGN0: Normal WB */
                    (0x1 << 8) | /* ORGN0: Normal WB */
                    (0x3 << 10)| /* SH0: Inner shareable */
                    (0x0 << 14); /* TG0: 4KB */
    asm volatile("msr VTCR_EL2, %0 \n\t isb" : : "r"(vtcr) : "memory");
}

void hac_s2_enable(void)
{
    uint64_t hcr;
    asm volatile("mrs %0, HCR_EL2" : "=r"(hcr));
    hcr |= (1ULL << 0);  /* VM: enable stage-2 */
    asm volatile("msr HCR_EL2, %0 \n\t isb" : : "r"(hcr) : "memory");
}

/* ---------------------------------------------------------------
 * HAC panic handlers
 * --------------------------------------------------------------- */
void hac_panic_sync(uint64_t esr, uint64_t far)
{
    hac_uart_puts("\r\n[HAC] PANIC: unexpected synchronous exception\r\n");
    (void)esr; (void)far;
    while (1) asm volatile("wfi");
}

void hac_irq_handler(void)
{
    /* Route to AIC handler → forward to EL1 or handle directly */
    /* TODO: full AIC interrupt routing */
}

void hac_fiq_handler(void)
{
    /* FIQ usually means secure timer or PMU event */
}

void hac_serror_handler(void)
{
    hac_uart_puts("\r\n[HAC] SError — hardware fault\r\n");
    while (1) asm volatile("wfi");
}
