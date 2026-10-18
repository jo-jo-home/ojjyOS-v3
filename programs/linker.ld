/* Freestanding EL0 samples. Created by Jonas Lee. */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x100010000;
    .text : { *(.text*) *(.rodata*) } :text
    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
