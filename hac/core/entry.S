/*
 * AgentOS HAC — EL2 Exception Vector Table
 *
 * ARM64 vector table layout (each entry 128 bytes / 32 instructions):
 *
 *  Offset  | Exception class
 *  --------|-----------------------------------------------
 *  0x000   | Synchronous – Current EL, SP_EL0
 *  0x080   | IRQ         – Current EL, SP_EL0
 *  0x100   | FIQ         – Current EL, SP_EL0
 *  0x180   | SError      – Current EL, SP_EL0
 *  0x200   | Synchronous – Current EL, SP_ELx
 *  0x280   | IRQ         – Current EL, SP_ELx   ← HVC from EL1 lands here via EL1→EL2 table
 *  0x300   | FIQ         – Current EL, SP_ELx
 *  0x380   | SError      – Current EL, SP_ELx
 *  0x400   | Synchronous – Lower EL (EL1), AArch64  ← HVC from EL1
 *  0x480   | IRQ         – Lower EL (EL1), AArch64
 *  0x500   | FIQ         – Lower EL (EL1), AArch64
 *  0x580   | SError      – Lower EL (EL1), AArch64
 *  0x600   | Synchronous – Lower EL (EL1), AArch32
 *  ...
 *
 * Target: AArch64 EL2, A11 Bionic, bare-metal
 */

.macro VENTRY label
    .align 7
    b \label
.endm

.section ".text.vectors", "ax"
.align 11  /* 2KB alignment required for VBAR_EL2 */
.global __hac_vectors
__hac_vectors:
    /* ---- Current EL with SP_EL0 ---- */
    VENTRY  _hac_sync_sp0
    VENTRY  _hac_irq_sp0
    VENTRY  _hac_fiq_sp0
    VENTRY  _hac_serror_sp0

    /* ---- Current EL with SP_EL2 ---- */
    VENTRY  _hac_sync_spx
    VENTRY  _hac_irq_spx
    VENTRY  _hac_fiq_spx
    VENTRY  _hac_serror_spx

    /* ---- Lower EL (EL1) AArch64 ---- */
    VENTRY  _hac_lower_sync_a64    /* HVC from EL1 falls here */
    VENTRY  _hac_lower_irq_a64
    VENTRY  _hac_lower_fiq_a64
    VENTRY  _hac_lower_serror_a64

    /* ---- Lower EL (EL1) AArch32 ---- */
    VENTRY  _hac_lower_sync_a32
    VENTRY  _hac_lower_irq_a32
    VENTRY  _hac_lower_fiq_a32
    VENTRY  _hac_lower_serror_a32

/* ---------------------------------------------------------------
 * Save/restore macros for full register context
 * --------------------------------------------------------------- */
.macro SAVE_REGS
    sub  sp, sp, #256
    stp  x0,  x1,  [sp, #0]
    stp  x2,  x3,  [sp, #16]
    stp  x4,  x5,  [sp, #32]
    stp  x6,  x7,  [sp, #48]
    stp  x8,  x9,  [sp, #64]
    stp  x10, x11, [sp, #80]
    stp  x12, x13, [sp, #96]
    stp  x14, x15, [sp, #112]
    stp  x16, x17, [sp, #128]
    stp  x18, x19, [sp, #144]
    stp  x20, x21, [sp, #160]
    stp  x22, x23, [sp, #176]
    stp  x24, x25, [sp, #192]
    stp  x26, x27, [sp, #208]
    stp  x28, x29, [sp, #224]
    mrs  x0,  ELR_EL2
    mrs  x1,  SPSR_EL2
    stp  x30, x0,  [sp, #240]
    str  x1,  [sp, #248]
.endm

.macro RESTORE_REGS
    ldr  x1,  [sp, #248]
    ldp  x30, x0, [sp, #240]
    msr  ELR_EL2, x0
    msr  SPSR_EL2, x1
    ldp  x28, x29, [sp, #224]
    ldp  x26, x27, [sp, #208]
    ldp  x24, x25, [sp, #192]
    ldp  x22, x23, [sp, #176]
    ldp  x20, x21, [sp, #160]
    ldp  x18, x19, [sp, #144]
    ldp  x16, x17, [sp, #128]
    ldp  x14, x15, [sp, #112]
    ldp  x12, x13, [sp, #96]
    ldp  x10, x11, [sp, #80]
    ldp  x8,  x9,  [sp, #64]
    ldp  x6,  x7,  [sp, #48]
    ldp  x4,  x5,  [sp, #32]
    ldp  x2,  x3,  [sp, #16]
    ldp  x0,  x1,  [sp, #0]
    add  sp, sp, #256
.endm

/* ---------------------------------------------------------------
 * HVC handler — called when EL1 kernel executes HVC #imm
 * Dispatcher: C function hvc_dispatch(x0..x7 as args, hvc_number)
 * --------------------------------------------------------------- */
_hac_lower_sync_a64:
    SAVE_REGS
    /* Determine if this is an HVC (EC = 0x16) */
    mrs  x9, ESR_EL2
    lsr  x10, x9, #26          /* Extract EC field [31:26] */
    and  x10, x10, #0x3F
    cmp  x10, #0x16            /* HVC from AArch64 */
    bne  _hac_lower_sync_unexpected
    /* x0-x7 already contain EL1 argument registers (preserved by SAVE_REGS reading from stack) */
    ldp  x0, x1, [sp, #0]
    ldp  x2, x3, [sp, #16]
    ldp  x4, x5, [sp, #32]
    ldp  x6, x7, [sp, #48]
    /* HVC immediate number is in ESR_EL2 [15:0] */
    and  x8, x9, #0xFFFF
    bl   hvc_dispatch           /* C function returns result in x0 */
    str  x0, [sp, #0]           /* write return value back to caller's x0 */
    RESTORE_REGS
    eret

_hac_lower_sync_unexpected:
    /* Log and panic */
    mrs  x0, ESR_EL2
    mrs  x1, FAR_EL2
    bl   hac_panic_sync
    /* unreachable */
    b    .

/* IRQ from EL1: forward to HAC IRQ handler */
_hac_lower_irq_a64:
    SAVE_REGS
    bl   hac_irq_handler
    RESTORE_REGS
    eret

/* FIQ from EL1 */
_hac_lower_fiq_a64:
    SAVE_REGS
    bl   hac_fiq_handler
    RESTORE_REGS
    eret

_hac_lower_serror_a64:
    SAVE_REGS
    bl   hac_serror_handler
    RESTORE_REGS
    eret

/* Stubs for other vector entries */
_hac_sync_sp0:
_hac_irq_sp0:
_hac_fiq_sp0:
_hac_serror_sp0:
    b .

_hac_sync_spx:
    SAVE_REGS
    mrs  x0, ESR_EL2
    mrs  x1, FAR_EL2
    bl   hac_panic_sync
    b    .

_hac_irq_spx:
    SAVE_REGS
    bl   hac_irq_handler
    RESTORE_REGS
    eret

_hac_fiq_spx:
    SAVE_REGS
    bl   hac_fiq_handler
    RESTORE_REGS
    eret

_hac_serror_spx:
_hac_lower_sync_a32:
_hac_lower_irq_a32:
_hac_lower_fiq_a32:
_hac_lower_serror_a32:
    b .

/* ---------------------------------------------------------------
 * Install vector table: called from hac_main() before enabling EL1
 * --------------------------------------------------------------- */
.global hac_install_vectors
hac_install_vectors:
    adr  x0, __hac_vectors
    msr  VBAR_EL2, x0
    isb
    ret
