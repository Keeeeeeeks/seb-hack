/**
 * STM32G474RETx_FLASH.ld
 * Linker script for STM32G474RE — Flash execution
 *
 * Memory map (RM0440 Table 2):
 *   Flash  : 512 KB @ 0x08000000  (main Flash bank)
 *   SRAM1  :  96 KB @ 0x20000000  (DMA-accessible — wavetable lives here)
 *   SRAM2  :  32 KB @ 0x20018000  (DMA-accessible)
 *   CCM    :  32 KB @ 0x10000000  (NOT DMA-accessible — fast code/data only)
 *
 * Stack: 4 KB at top of SRAM1.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);   /* top of SRAM1 */

_Min_Heap_Size  = 0x400;   /* 1 KB heap  (unused — static alloc only) */
_Min_Stack_Size = 0x1000;  /* 4 KB stack */

MEMORY
{
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  SRAM1   (xrw) : ORIGIN = 0x20000000, LENGTH = 96K
  SRAM2   (xrw) : ORIGIN = 0x20018000, LENGTH = 32K
  CCMRAM  (xrw) : ORIGIN = 0x10000000, LENGTH = 32K
}

SECTIONS
{
  /* ---- Vector table + code -------------------------------------------- */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* ---- CCM SRAM (fast code/data, NOT DMA-accessible) ------------------ */
  _siccmram = LOADADDR(.ccmram);

  .ccmram :
  {
    . = ALIGN(4);
    _sccmram = .;
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(4);
    _eccmram = .;
  } >CCMRAM AT> FLASH

  /* ---- Initialised data (copied Flash→SRAM1 by startup) --------------- */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >SRAM1 AT> FLASH

  /* ---- Zero-initialised data ------------------------------------------ */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SRAM1

  /* ---- Heap + stack sanity check -------------------------------------- */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >SRAM1

  /* ---- SRAM2 (available for future use) ------------------------------- */
  .sram2 (NOLOAD) :
  {
    . = ALIGN(4);
    *(.sram2)
    *(.sram2*)
    . = ALIGN(4);
  } >SRAM2

  /* ---- Discard unwanted sections -------------------------------------- */
  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
