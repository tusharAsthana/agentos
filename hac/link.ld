/*
 * AgentOS HAC EL2 Bare-Metal Linker Script
 * 
 * Memory Layout:
 *   0x820000000 – 0x823FFFFFF : HAC firmware (64 MB)
 *     - 0x820000000 : EL3 monitor entry (4 KB)
 *     - 0x820001000 : EL2 boot code + exception vectors
 *     - 0x820010000 : HAC main code
 *     - 0x821000000 : HAC .rodata (read-only data)
 *     - 0x822000000 : HAC .data + .bss (writable, 32 MB for heap)
 *
 * Entry point: _start (defined in entry.S)
 */

ENTRY(_start)

SECTIONS
{
  /* ============== EL3 Secure Monitor (4 KB) ============== */
  . = 0x820000000;
  
  .monitor : {
    KEEP(*(.monitor.text))
  } :monitor
  
  . = ALIGN(0x1000);  /* 4 KB alignment */
  
  /* ============== EL2 Boot & Exception Vectors ============== */
  . = 0x820001000;
  
  .text : {
    *(.text.entry)        /* entry.S _start */
    *(.text.exceptions)   /* exception vectors */
    *(.text.init)         /* early init code */
    *(.text)              /* general code */
    *(.text.*)
  } :ram AT > rom
  
  . = ALIGN(0x1000);
  
  /* ============== Read-Only Data ============== */
  . = 0x821000000;
  
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    *(.data.rel.ro)
    *(.data.rel.ro.*)
  } :ram AT > rom
  
  . = ALIGN(0x100);
  
  /* ============== Initialized Data ============== */
  . = 0x822000000;
  
  .data : {
    *(.data)
    *(.data.*)
    CONSTRUCTORS
  } :ram AT > rom
  
  . = ALIGN(0x100);
  
  /* ============== Uninitialized Data (BSS) ============== */
  .bss : {
    __bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(0x1000);
    __bss_end = .;
  } :ram
  
  /* ============== HAC Heap (dynamic allocation) ============== */
  __heap_start = .;
  . += 0x1800000;  /* 24 MB heap */
  __heap_end = .;
  
  /* ============== HAC Stack (grows downward) ============== */
  __stack_end = 0x823F00000;  /* Bottom of stack (grows down) */
  . = __stack_end;
  __stack_start = . + 0x100000;  /* 1 MB stack */
  
  /* ============== IPC Shared Memory (mapped by kernel) ============== */
  /* Note: IPC ring buffers at 0x84E000000, visible to both EL2 and EL1 */
  /* This linker script does not allocate it; kernel IOMMU maps it */
  
  /* ============== Device Tree & Initrd (loaded by bootloader) ============== */
  /* Note: Device tree at 0x84F000000, initrd at 0x84F100000 */
  
  /* ============== Discard sections ============== */
  /DISCARD/ : {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.gnu.lto_*)
    *(.gcc_except_table)
  }
}

/* ============== Segment Definitions ============== */
PHDRS
{
  monitor   PT_LOAD FLAGS(4);       /* PF_R (read-only) */
  ram       PT_LOAD FLAGS(7);       /* PF_RWX (read, write, execute) */
}

/* ============== Symbol Exports for C Code ============== */

__text_start = ADDR(.text);
__text_end = ADDR(.text) + SIZEOF(.text);

__rodata_start = ADDR(.rodata);
__rodata_end = ADDR(.rodata) + SIZEOF(.rodata);

__data_start = ADDR(.data);
__data_end = ADDR(.data) + SIZEOF(.data);

__init_array_start = ADDR(.init_array);
__init_array_end = ADDR(.init_array) + SIZEOF(.init_array);

__fini_array_start = ADDR(.fini_array);
__fini_array_end = ADDR(.fini_array) + SIZEOF(.fini_array);

/* Linker relaxation features */
__DYNAMIC = 0;
