ENTRY(_start)

SECTIONS {
  . = 0x08000000;
  .text : {
    KEEP(*(.text.boot))
    *(.text*)
    *(.rodata*)
  }
}
