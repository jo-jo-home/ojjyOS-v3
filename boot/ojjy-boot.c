/*
 * ojjyOS v3 UEFI Bootloader
 *
 * This is a minimal UEFI application that:
 * 1. Gets the framebuffer via GOP (Graphics Output Protocol)
 * 2. Gets the memory map from UEFI
 * 3. Loads the kernel from the ESP
 * 4. Exits boot services and jumps to kernel
 *
 * Build: Compiled as PE+ executable for x86_64 UEFI
 */

#include <efi.h>
#include <efilib.h>

/* Boot information passed to kernel */
typedef struct {
    /* Framebuffer info */
    UINT64 fb_addr;
    UINT32 fb_width;
    UINT32 fb_height;
    UINT32 fb_pitch;        /* Bytes per scanline */
    UINT32 fb_bpp;          /* Bits per pixel */

    /* Memory map */
    UINT64 mmap_addr;
    UINT64 mmap_size;
    UINT64 mmap_desc_size;
    UINT32 mmap_desc_version;

    /* ACPI RSDP address (for later use) */
    UINT64 rsdp_addr;
} __attribute__((packed)) BootInfo;

/* Kernel entry point signature */
typedef void (*KernelEntry)(BootInfo *info);

/* Fixed addresses - kernel loaded at 1MB, boot info at 0x500 */
#define KERNEL_LOAD_ADDR    0x100000
#define BOOT_INFO_ADDR      0x500
#define KERNEL_PATH         L"\\EFI\\ojjyos\\kernel.bin"

/* Forward declarations */
EFI_STATUS LoadKernel(EFI_HANDLE ImageHandle, UINT64 *entry_point);
EFI_STATUS SetupFramebuffer(BootInfo *info);
EFI_STATUS GetMemoryMap(BootInfo *info, UINTN *map_key);
UINT64 FindRSDP(void);

/*
 * EFI entry point
 */
EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    BootInfo *boot_info;
    UINT64 kernel_entry;
    UINTN map_key;

    /* Initialize GNU-EFI library */
    InitializeLib(ImageHandle, SystemTable);

    /* Clear screen and show banner */
    ST->ConOut->ClearScreen(ST->ConOut);
    Print(L"ojjyOS v3 Bootloader\n");
    Print(L"====================\n\n");

    /* Setup boot info structure at fixed address */
    boot_info = (BootInfo *)BOOT_INFO_ADDR;
    SetMem(boot_info, sizeof(BootInfo), 0);

    /* Step 1: Setup framebuffer via GOP */
    Print(L"[BOOT] Initializing framebuffer...\n");
    status = SetupFramebuffer(boot_info);
    if (EFI_ERROR(status)) {
        Print(L"[FAIL] Could not initialize framebuffer: %r\n", status);
        goto error;
    }
    Print(L"[OK]   Framebuffer: %dx%d @ 0x%lx\n",
          boot_info->fb_width, boot_info->fb_height, boot_info->fb_addr);

    /* Step 2: Find ACPI RSDP (optional, for later) */
    Print(L"[BOOT] Locating ACPI tables...\n");
    boot_info->rsdp_addr = FindRSDP();
    if (boot_info->rsdp_addr) {
        Print(L"[OK]   RSDP found at 0x%lx\n", boot_info->rsdp_addr);
    } else {
        Print(L"[WARN] RSDP not found\n");
    }

    /* Step 3: Load kernel from disk */
    Print(L"[BOOT] Loading kernel from %s...\n", KERNEL_PATH);
    status = LoadKernel(ImageHandle, &kernel_entry);
    if (EFI_ERROR(status)) {
        Print(L"[FAIL] Could not load kernel: %r\n", status);
        goto error;
    }
    Print(L"[OK]   Kernel loaded, entry at 0x%lx\n", kernel_entry);

    /* Step 4: Get memory map (must be done last before ExitBootServices) */
    Print(L"[BOOT] Getting memory map...\n");
    status = GetMemoryMap(boot_info, &map_key);
    if (EFI_ERROR(status)) {
        Print(L"[FAIL] Could not get memory map: %r\n", status);
        goto error;
    }
    Print(L"[OK]   Memory map: %d bytes, desc size %d\n",
          (UINT32)boot_info->mmap_size, (UINT32)boot_info->mmap_desc_size);

    /* Step 5: Exit boot services - point of no return */
    Print(L"[BOOT] Exiting boot services...\n");
    status = BS->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(status)) {
        /* Memory map may have changed, try once more */
        status = GetMemoryMap(boot_info, &map_key);
        if (EFI_ERROR(status)) {
            Print(L"[FAIL] Could not refresh memory map\n");
            goto error;
        }
        status = BS->ExitBootServices(ImageHandle, map_key);
        if (EFI_ERROR(status)) {
            Print(L"[FAIL] Could not exit boot services: %r\n", status);
            goto error;
        }
    }

    /* Boot services are now gone - no more Print(), no more UEFI calls */

    /* Jump to kernel */
    KernelEntry entry = (KernelEntry)kernel_entry;
    entry(boot_info);

    /* Should never return */
    while (1) {
        __asm__ volatile("hlt");
    }

error:
    Print(L"\n[BOOT] Press any key to reboot...\n");
    WaitForSingleEvent(ST->ConIn->WaitForKey, 0);
    RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    return status;
}

/*
 * Setup framebuffer using UEFI Graphics Output Protocol
 */
EFI_STATUS
SetupFramebuffer(BootInfo *info)
{
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info;
    UINTN mode_info_size;
    UINTN num_modes, best_mode;
    UINT32 best_width = 0;

    /* Locate GOP */
    status = BS->LocateProtocol(&gop_guid, NULL, (void **)&gop);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Find best mode (prefer 1920x1080, or largest available) */
    num_modes = gop->Mode->MaxMode;
    best_mode = gop->Mode->Mode;  /* Default to current */

    for (UINTN i = 0; i < num_modes; i++) {
        status = gop->QueryMode(gop, i, &mode_info_size, &mode_info);
        if (EFI_ERROR(status)) continue;

        /* Only accept 32-bit pixel formats */
        if (mode_info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor &&
            mode_info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor) {
            continue;
        }

        /* Prefer 1920x1080 */
        if (mode_info->HorizontalResolution == 1920 &&
            mode_info->VerticalResolution == 1080) {
            best_mode = i;
            best_width = 1920;
            break;
        }

        /* Otherwise take largest */
        if (mode_info->HorizontalResolution > best_width) {
            best_width = mode_info->HorizontalResolution;
            best_mode = i;
        }
    }

    /* Set the mode if different */
    if (best_mode != gop->Mode->Mode) {
        status = gop->SetMode(gop, best_mode);
        if (EFI_ERROR(status)) {
            /* Fall back to current mode */
            best_mode = gop->Mode->Mode;
        }
    }

    /* Fill in framebuffer info */
    info->fb_addr = gop->Mode->FrameBufferBase;
    info->fb_width = gop->Mode->Info->HorizontalResolution;
    info->fb_height = gop->Mode->Info->VerticalResolution;
    info->fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;  /* 4 bytes per pixel */
    info->fb_bpp = 32;

    return EFI_SUCCESS;
}

/*
 * Load kernel binary from ESP
 */
EFI_STATUS
LoadKernel(EFI_HANDLE ImageHandle, UINT64 *entry_point)
{
    EFI_STATUS status;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *kernel_file;
    EFI_FILE_INFO *file_info;
    UINTN info_size;
    UINT64 kernel_size;
    UINT8 *kernel_buffer;

    /* Get filesystem protocol */
    status = BS->LocateProtocol(&fs_guid, NULL, (void **)&fs);
    if (EFI_ERROR(status)) {
        /* Try to get it from the loaded image's device */
        EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
        EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

        status = BS->HandleProtocol(ImageHandle, &li_guid, (void **)&loaded_image);
        if (EFI_ERROR(status)) return status;

        status = BS->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, (void **)&fs);
        if (EFI_ERROR(status)) return status;
    }

    /* Open root directory */
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) return status;

    /* Open kernel file */
    status = root->Open(root, &kernel_file, KERNEL_PATH, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    /* Get file size */
    info_size = sizeof(EFI_FILE_INFO) + 256;  /* Extra space for filename */
    status = BS->AllocatePool(EfiLoaderData, info_size, (void **)&file_info);
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    status = kernel_file->GetInfo(kernel_file, &fi_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        BS->FreePool(file_info);
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    kernel_size = file_info->FileSize;
    BS->FreePool(file_info);

    /* Allocate memory for kernel at fixed address */
    kernel_buffer = (UINT8 *)KERNEL_LOAD_ADDR;
    UINTN pages = (kernel_size + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_ADDR;

    status = BS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(status)) {
        /* Try allocating anywhere and copying */
        status = BS->AllocatePool(EfiLoaderData, kernel_size, (void **)&kernel_buffer);
        if (EFI_ERROR(status)) {
            kernel_file->Close(kernel_file);
            root->Close(root);
            return status;
        }
    }

    /* Read kernel */
    UINTN read_size = kernel_size;
    status = kernel_file->Read(kernel_file, &read_size, kernel_buffer);

    kernel_file->Close(kernel_file);
    root->Close(root);

    if (EFI_ERROR(status)) return status;

    /* Kernel entry point is at the start of the binary */
    *entry_point = KERNEL_LOAD_ADDR;

    return EFI_SUCCESS;
}

/*
 * Get UEFI memory map
 */
EFI_STATUS
GetMemoryMap(BootInfo *info, UINTN *map_key)
{
    EFI_STATUS status;
    UINTN mmap_size = 0;
    UINTN desc_size;
    UINT32 desc_version;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;

    /* First call to get required size */
    status = BS->GetMemoryMap(&mmap_size, mmap, map_key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    /* Allocate buffer (add extra space for the allocation itself) */
    mmap_size += 2 * desc_size;
    status = BS->AllocatePool(EfiLoaderData, mmap_size, (void **)&mmap);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Get actual memory map */
    status = BS->GetMemoryMap(&mmap_size, mmap, map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status)) {
        BS->FreePool(mmap);
        return status;
    }

    /* Store in boot info */
    info->mmap_addr = (UINT64)mmap;
    info->mmap_size = mmap_size;
    info->mmap_desc_size = desc_size;
    info->mmap_desc_version = desc_version;

    return EFI_SUCCESS;
}

/*
 * Find ACPI RSDP from UEFI configuration tables
 */
UINT64
FindRSDP(void)
{
    EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;
    EFI_GUID acpi10_guid = ACPI_TABLE_GUID;

    /* Search configuration tables for ACPI */
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];

        /* Prefer ACPI 2.0 */
        if (CompareGuid(&table->VendorGuid, &acpi20_guid) == 0) {
            return (UINT64)table->VendorTable;
        }

        /* Fall back to ACPI 1.0 */
        if (CompareGuid(&table->VendorGuid, &acpi10_guid) == 0) {
            return (UINT64)table->VendorTable;
        }
    }

    return 0;
}
