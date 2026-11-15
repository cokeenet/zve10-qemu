SECTIONS {
 . = 0x0700d708;
 .text : { *(.text*) *(.rodata*) . = ALIGN(4); }
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) }
}
