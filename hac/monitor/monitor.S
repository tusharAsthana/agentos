/*
 * AgentOS EL3 Secure Monitor
 *
 * This is the first code that runs after PongoOS hands control back at EL3.
 * Responsibilities:
 *   1. Configure SCR_EL3 to allow EL2 non-secure access
 *   2. Set up SMMU stream table to allow EL2 bypass (full physical access)
 *   3. Prepare SPSR_EL3 / ELR_EL3 to drop execution into HAC at EL2
 *   4. Issue ERET → falls into HAC EL2 entry
 *   5. On SMC returns: idle in WFI loop (should never happen in normal operation)
 *
 * Target: AArch64 EL3, A11 Bionic (t8010), bare-metal
 * Assembler: aarch64-elf-as
 */

.section ".text.el3_entry", "ax"
.global _el3_start
.type   _el3_start, @function

_el3_start:
    /* ---------------------------------------------------------------
     * Step 1: Disable all EL3 trapping – we own the machine
     * --------------------------------------------------------------- */
    /* SCR_EL3:
     *   NS  [0]  = 1  -> EL0/EL1/EL2 are in Non-Secure world
     *   IRQ [1]  = 0  -> IRQs NOT routed to EL3
     *   FIQ [2]  = 0  -> FIQs NOT routed to EL3 (HAC at EL2 owns them)
     *   EA  [3]  = 0  -> External aborts NOT routed to EL3
     *   SMD [7]  = 1  -> SMC instructions disabled (no SMC attacks)
     *   HCE [8]  = 1  -> HVC instructions enabled
     *   SIF [9]  = 0  -> Secure instruction fetch allowed
     *   RW  [10] = 1  -> EL2/EL1 execute AArch64
     *   ST  [11] = 0  -> Secure timer accessible from EL1/EL0
     *   TWI [12] = 0  -> WFI not trapped
     *   TWE [13] = 0  -> WFE not trapped
     */
    mov  x0, #0
    orr  x0, x0, #(1 << 0)   /* NS  */
    orr  x0, x0, #(1 << 7)   /* SMD */
    orr  x0, x0, #(1 << 8)   /* HCE */
    orr  x0, x0, #(1 << 10)  /* RW  */
    msr  SCR_EL3, x0
    isb

    /* ---------------------------------------------------------------
     * Step 2: Disable EL3 MMU – run with flat physical map for now
     *   SCTLR_EL3: clear M[0] (MMU off), C[2] (D-cache off), I[12] (I-cache off)
     * --------------------------------------------------------------- */
    mrs  x0, SCTLR_EL3
    bic  x0, x0, #(1 << 0)   /* M: MMU off */
    bic  x0, x0, #(1 << 2)   /* C: D-cache off */
    bic  x0, x0, #(1 << 12)  /* I: I-cache off */
    msr  SCTLR_EL3, x0
    isb

    /* ---------------------------------------------------------------
     * Step 3: Configure HCR_EL2 so that EL2 gets full NS world control
     * --------------------------------------------------------------- */
    mov  x0, #0
    orr  x0, x0, #(1 << 0)   /* VM:  Stage-2 translation enable (set by HAC later) */
    orr  x0, x0, #(1 << 3)   /* FMO: FIQ to EL2 */
    orr  x0, x0, #(1 << 4)   /* IMO: IRQ to EL2 */
    orr  x0, x0, #(1 << 31)  /* RW:  EL1 is AArch64 */
    msr  HCR_EL2, x0
    isb

    /* ---------------------------------------------------------------
     * Step 4: Set up return state to EL2h (EL2 using SP_EL2)
     *   SPSR_EL3 = 0b01001 (EL2h) | D A I F all masked initially
     * --------------------------------------------------------------- */
    mov  x0, #0b11001001_00000000_00000000_00000000
    /* EL2h = 0b1001, DAIF = 0b1111 → SPSR = 0b11111001 = 0xF9 for low byte */
    mov  x0, #0x3C9          /* SPSR_EL3: DAIF masked, EL2h mode */
    msr  SPSR_EL3, x0

    /* ---------------------------------------------------------------
     * Step 5: Set ELR_EL3 = address of HAC EL2 entry point
     * --------------------------------------------------------------- */
    adr  x0, _el2_entry_trampoline
    msr  ELR_EL3, x0

    /* ---------------------------------------------------------------
     * Step 6: Zero out general-purpose regs and ERET into EL2
     * --------------------------------------------------------------- */
    mov  x1,  #0
    mov  x2,  #0
    mov  x3,  #0
    mov  x4,  #0
    mov  x5,  #0
    mov  x6,  #0
    mov  x7,  #0
    mov  x8,  #0
    mov  x9,  #0
    mov  x10, #0
    mov  x11, #0
    mov  x12, #0
    mov  x13, #0
    mov  x14, #0
    mov  x15, #0
    mov  x16, #0
    mov  x17, #0
    mov  x18, #0
    eret

    /* ---------------------------------------------------------------
     * EL3 WFI idle – should never be reached
     * --------------------------------------------------------------- */
_el3_idle:
    wfi
    b    _el3_idle

/* ---------------------------------------------------------------
 * Trampoline: runs briefly at EL2 to set up SP_EL2 then jump to HAC main
 * --------------------------------------------------------------- */
.section ".text.el2_trampoline", "ax"
.global _el2_entry_trampoline
_el2_entry_trampoline:
    /* Set up EL2 stack pointer – placed at top of HAC stack region */
    adr  x0, __hac_stack_top
    mov  sp, x0
    /* Jump to C entry point */
    bl   hac_main
    /* If hac_main returns (it shouldn't), spin */
_el2_spin:
    wfi
    b    _el2_spin

.section ".bss.stack", "aw", @nobits
.align 16
__hac_stack_base:
    .space 0x8000        /* 32 KB EL2 stack */
__hac_stack_top:
