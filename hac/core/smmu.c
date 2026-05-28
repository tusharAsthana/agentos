/*
 * AgentOS HAC — SMMU (System MMU) Initialization
 *
 * Apple uses a proprietary DART (Device Address Resolution Table) on A-series
 * chips, not the ARM SMMUv2/v3 spec. However, the concepts are similar:
 * each DMA master has a stream ID; the DART maps IPA → PA for each stream.
 *
 * For HAC purposes:
 *   - Disable DART translation for HAC-owned peripherals (EL2 controls them)
 *   - Keep DART enabled for modem / WiFi so they stay in their sandboxes
 *   - The Linux kernel's DMA drivers will re-configure DART entries as needed
 *     via HAC primitives (HAC mediates all DART access)
 *
 * DART base addresses extracted from Apple Device Tree.
 */
#include "hac.h"
#include "../mmio/a11_mmio.h"

/* DART instances on A11 (one per major DMA subsystem) */
#define DART_ANS_BASE       0x200000000ULL   /* ANS2 storage */
#define DART_USB_BASE       0x20C010000ULL   /* USB DMA */
#define DART_PCIE_BASE      0x300010000ULL   /* PCIe WiFi */
#define DART_ISP_BASE       0x22A010000ULL   /* Camera ISP */
#define DART_DISP_BASE      0x230010000ULL   /* Display */

/* DART register offsets */
#define DART_PARAMS2        0x0004
#define DART_TLB_OP         0x0020
#define DART_TLB_INV_ALL    BIT(1)
#define DART_CONFIG         0x0060
#define DART_CONFIG_BYPASS  BIT(2)    /* Bypass translation */
#define DART_CONFIG_LOCK    BIT(15)   /* Lock config register */
#define DART_STREAM_VIRT    0x0100    /* Stream mapping tables */
#define DART_ERROR_STATUS   0x0040
#define DART_ERROR_ADDR_LO  0x0050
#define DART_ERROR_ADDR_HI  0x0054

static void dart_set_bypass(uintptr_t dart_base)
{
    /* Read current config, set bypass, clear lock */
    uint32_t cfg = MMIO_READ32(dart_base + DART_CONFIG);
    cfg |= DART_CONFIG_BYPASS;
    cfg &= ~DART_CONFIG_LOCK;
    MMIO_WRITE32(dart_base + DART_CONFIG, cfg);

    /* Invalidate TLB */
    MMIO_WRITE32(dart_base + DART_TLB_OP, DART_TLB_INV_ALL);

    /* Wait for invalidation to complete (poll TLB_OP bit clear) */
    unsigned timeout = 10000;
    while ((MMIO_READ32(dart_base + DART_TLB_OP) & DART_TLB_INV_ALL) && --timeout)
        asm volatile("nop");
}

void smmu_init(void)
{
    hac_uart_puts("[SMMU] Configuring DART instances...\r\n");

    /*
     * Grant EL2-controlled peripherals full bypass:
     * HAC primitives talk directly to these hardware blocks.
     */
    dart_set_bypass(DART_ANS_BASE);   /* Storage: HAC owns DMA */
    dart_set_bypass(DART_ISP_BASE);   /* Camera: HAC owns DMA */
    dart_set_bypass(DART_DISP_BASE);  /* Display: HAC owns DMA */
    dart_set_bypass(DART_USB_BASE);   /* USB: shared with kernel */

    /*
     * WiFi (PCIe) DART: keep active but unlocked.
     * Linux kernel will configure it via HAC storage primitives.
     */
    uint32_t pcie_cfg = MMIO_READ32(DART_PCIE_BASE + DART_CONFIG);
    pcie_cfg &= ~DART_CONFIG_LOCK;
    MMIO_WRITE32(DART_PCIE_BASE + DART_CONFIG, pcie_cfg);

    hac_uart_puts("[SMMU] DART initialization complete\r\n");
}

void smmu_map_stream(uint32_t stream_id, phys_t pa, size_t size)
{
    /* Placeholder for per-stream DART table entry programming.
     * Full implementation: write page table entries to DART_STREAM_VIRT,
     * invalidate TLB, and enable translation for this stream.
     */
    (void)stream_id; (void)pa; (void)size;
    hac_uart_puts("[SMMU] smmu_map_stream: not fully implemented\r\n");
}
