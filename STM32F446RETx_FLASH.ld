ENTRY(Reset_Handler)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K }
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
 .isr_vector : { KEEP(*(.isr_vector)) } >FLASH
 .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } >FLASH
 .ARM.exidx : { *(.ARM.exidx*) } >FLASH
 .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
 .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
 .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH
 _sidata = LOADADDR(.data);
 .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } >RAM AT>FLASH
 .bss : { _sbss = .; __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = .; } >RAM
 end = .; _end = .;
}
