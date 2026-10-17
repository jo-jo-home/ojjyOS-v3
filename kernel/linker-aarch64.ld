/*
 * ojjyOS v3 Kernel Linker Script (aarch64)
 *
 * On QEMU -M virt, RAM begins at 0x40000000 (1 GB).
 * The kernel is linked and loaded at 0x41000000 (+16 MB into RAM),
 * leaving the first 16 MB of RAM free for UEFI firmware tables.
 * Entry point is _start in arch/aarch64/entry.S.
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x41000000 */
    . = 0x41000000;
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
