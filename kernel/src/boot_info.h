/*
 * ojjyOS v3 Kernel - Boot Information Structure
 *
 * This structure is filled by the UEFI bootloader and passed to the kernel.
 * Must match the definition in boot/ojjy-boot.c exactly.
 */

#ifndef _OJJY_BOOT_INFO_H
#define _OJJY_BOOT_INFO_H

#include "types.h"

/*
 * Boot information passed from UEFI bootloader
 */
typedef struct {
    /* Framebuffer info */
    uint64_t fb_addr;       /* Physical address of framebuffer */
    uint32_t fb_width;      /* Width in pixels */
    uint32_t fb_height;     /* Height in pixels */
    uint32_t fb_pitch;      /* Bytes per scanline */
    uint32_t fb_bpp;        /* Bits per pixel (32) */

    /* Memory map from UEFI */
    uint64_t mmap_addr;     /* Address of memory map array */
    uint64_t mmap_size;     /* Total size of memory map in bytes */
    uint64_t mmap_desc_size;/* Size of each descriptor */
    uint32_t mmap_desc_version;

    /* ACPI */
    uint64_t rsdp_addr;     /* ACPI RSDP address */
} PACKED BootInfo;

/*
 * UEFI Memory types (subset we care about)
 */
#define EFI_RESERVED_MEMORY_TYPE        0
#define EFI_LOADER_CODE                 1
#define EFI_LOADER_DATA                 2
#define EFI_BOOT_SERVICES_CODE          3
#define EFI_BOOT_SERVICES_DATA          4
#define EFI_RUNTIME_SERVICES_CODE       5
#define EFI_RUNTIME_SERVICES_DATA       6
#define EFI_CONVENTIONAL_MEMORY         7
#define EFI_UNUSABLE_MEMORY             8
#define EFI_ACPI_RECLAIM_MEMORY         9
#define EFI_ACPI_MEMORY_NVS             10
#define EFI_MEMORY_MAPPED_IO            11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                    13

/*
 * UEFI Memory Descriptor
 */
typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint64_t num_pages;
    uint64_t attribute;
} PACKED EfiMemoryDescriptor;

/* Global boot info pointer */
extern BootInfo *g_boot_info;

#endif /* _OJJY_BOOT_INFO_H */
