/*
 * ojjyOS v3 Kernel Linker Script (aarch64)
 *
 * Linked and loaded at 0x71000000, which is RAM on both supported
 * machines: QEMU virt (RAM 0x40000000..) and Apple's Virtualization
 * framework (RAM 0x70000000..0xF0000000; 0x41000000 is not RAM there).
 * Keep big buffers out of BSS (see uefi-kernel-bss notes in AGENTS.md).
 * Entry point is _start in arch/aarch64/entry.S.
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x71000000 */
    . = 0x71000000;
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
