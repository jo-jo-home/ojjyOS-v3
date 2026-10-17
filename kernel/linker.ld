/*
 * ojjyOS v3 Kernel Linker Script
 *
 * Kernel is loaded at 64MB (0x4000000) by the bootloader. Modern OVMF
 * reserves ACPI NVS and boot-services ranges around 8-24MB, which the
 * growing static BSS cannot safely straddle when linked at 1MB.
 * Entry point is _start in entry.asm.
 */

ENTRY(_start)

SECTIONS
{
    /* Keep the fixed-address kernel above OVMF's reserved low-memory area. */
    . = 0x4000000;
    _kernel_start = .;

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
