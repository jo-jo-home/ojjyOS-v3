/*
 * ojjyOS v3 Kernel Linker Script
 *
 * Kernel is loaded at 1MB (0x100000) by the bootloader.
 * Entry point is _start in entry.asm.
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB */
    . = 0x100000;

    /* Code section */
    .text : ALIGN(4K)
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    /* BSS (uninitialized data) */
    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }

    /* End of kernel */
    _kernel_end = .;
}
