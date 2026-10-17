/*
 * ojjyOS v3 UEFI Bootloader
 *
 * This is a minimal UEFI application that:
 * 1. Gets the framebuffer via GOP (Graphics Output Protocol)
 * 2. Gets the memory map from UEFI
 * 3. Loads the kernel (an ELF64 executable) from the ESP
 * 4. Exits boot services and jumps to the kernel entry point
 *
 * Build: Compiled as PE+ executable for x86_64 UEFI
 *
 * ojjyOS — Created by Jonas Lee
 */

#ifdef OJJY_STANDALONE_EFI
#include "efi_minimal.h"
#else
#include <efi.h>
#include <efilib.h>
#endif

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

/* Fixed addresses - boot info at 0x500 (below 1MB, reserved by kernel PMM) */
#define BOOT_INFO_ADDR      0x500
#define KERNEL_PATH         L"\\EFI\\ojjyos\\kernel.bin"

/*
 * Minimal ELF64 definitions (we only need what we validate)
 */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define ET_EXEC         2
#define EM_X86_64       62
#define EM_AARCH64      183
#define PT_LOAD         1

#if defined(__x86_64__)
  #define OJJY_EXPECTED_MACHINE EM_X86_64
#elif defined(__aarch64__)
  #define OJJY_EXPECTED_MACHINE EM_AARCH64
#else
  #error "Unsupported boot architecture"
#endif

typedef struct {
    UINT8   e_ident[16];
    UINT16  e_type;
    UINT16  e_machine;
    UINT32  e_version;
    UINT64  e_entry;
    UINT64  e_phoff;
    UINT64  e_shoff;
    UINT32  e_flags;
    UINT16  e_ehsize;
    UINT16  e_phentsize;
    UINT16  e_phnum;
    UINT16  e_shentsize;
    UINT16  e_shnum;
    UINT16  e_shstrndx;
} __attribute__((packed)) Elf64Ehdr;

typedef struct {
    UINT32  p_type;
    UINT32  p_flags;
    UINT64  p_offset;
    UINT64  p_vaddr;
    UINT64  p_paddr;
    UINT64  p_filesz;
    UINT64  p_memsz;
    UINT64  p_align;
} __attribute__((packed)) Elf64Phdr;

#define MAX_PHDRS 16

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
    BootInfo *boot_info = NULL;
    UINT64 kernel_entry;
    UINTN map_key;

    /* Initialize GNU-EFI library */
    InitializeLib(ImageHandle, SystemTable);

    /* Disable the UEFI watchdog timer (5 min default) */
    BS->SetWatchdogTimer(0, 0, 0, NULL);

    /* Clear screen and show banner */
    ST->ConOut->ClearScreen(ST->ConOut);
    Print(L"ojjyOS v3 Bootloader\n");
    Print(L"====================\n\n");

    /* Allocate boot info in EfiLoaderData RAM (preserved by kernel PMM).
     * On aarch64 QEMU virt, 0x500 is pflash0 ROM, so dynamic allocation is
     * required; fall back to BOOT_INFO_ADDR on x86 if AllocatePool fails. */
    status = BS->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void **)&boot_info);
    if (EFI_ERROR(status) || !boot_info) {
        boot_info = (BootInfo *)BOOT_INFO_ADDR;
    }
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
    status = GetMemoryMap(boot_info, &map_key);
    if (EFI_ERROR(status)) {
        Print(L"[FAIL] Could not get memory map: %r\n", status);
        goto error;
    }
    Print(L"[OK]   Memory map: %d bytes, desc size %d\n",
          (UINT32)boot_info->mmap_size, (UINT32)boot_info->mmap_desc_size);

    /* Step 5: Exit boot services - point of no return.
     * The map key can be invalidated by anything the firmware does,
     * so retry (with a fresh memory map) if the first attempt fails. */
    status = BS->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(status)) {
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
#ifdef __aarch64__
        __asm__ volatile("wfi");
#else
        __asm__ volatile("hlt");
#endif
    }

error:
    Print(L"\n[BOOT] Press any key to reboot...\n");
    WaitForSingleEvent(ST->ConIn->WaitForKey, 0);
    RT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    return status;
}

/*
 * Setup framebuffer using UEFI Graphics Output Protocol.
 *
 * The kernel assumes a 32-bpp BGRA (blue first) framebuffer, so we only
 * accept PixelBlueGreenRedReserved8BitPerColor modes - including
 * validating whatever mode the firmware is already in.
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
    BOOLEAN have_best = FALSE;

    /* Locate GOP */
    status = BS->LocateProtocol(&gop_guid, NULL, (void **)&gop);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* If the current mode is already a 32-bpp BGRx mode, it's a candidate */
    if (gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        best_mode = gop->Mode->Mode;
        best_width = gop->Mode->Info->HorizontalResolution;
        have_best = TRUE;
    }

    /* Find best mode (prefer 1920x1080, otherwise largest available) */
    num_modes = gop->Mode->MaxMode;

    for (UINTN i = 0; i < num_modes; i++) {
        status = gop->QueryMode(gop, i, &mode_info_size, &mode_info);
        if (EFI_ERROR(status)) continue;

        /* Only accept 32-bit BGRA pixel format (kernel assumes BGRA order) */
        if (mode_info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
            BS->FreePool(mode_info);
            continue;
        }

        /* Prefer 1920x1080 */
        if (mode_info->HorizontalResolution == 1920 &&
            mode_info->VerticalResolution == 1080) {
            best_mode = i;
            best_width = 1920;
            have_best = TRUE;
            BS->FreePool(mode_info);
            break;
        }

        /* Otherwise take largest */
        if (mode_info->HorizontalResolution > best_width) {
            best_width = mode_info->HorizontalResolution;
            best_mode = i;
            have_best = TRUE;
        }

        BS->FreePool(mode_info);
    }

    /* No usable mode at all? */
    if (!have_best) {
        return EFI_UNSUPPORTED;
    }

    /* Set the mode if different */
    if (best_mode != gop->Mode->Mode) {
        status = gop->SetMode(gop, best_mode);
        if (EFI_ERROR(status)) {
            /* Fall back to current mode - re-validate it */
            if (gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
                return EFI_UNSUPPORTED;
            }
        }
    }

    /* Fill in framebuffer info (validate final state) */
    if (gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
        return EFI_UNSUPPORTED;
    }

    info->fb_addr = gop->Mode->FrameBufferBase;
    info->fb_width = gop->Mode->Info->HorizontalResolution;
    info->fb_height = gop->Mode->Info->VerticalResolution;
    info->fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;  /* 4 bytes per pixel */
    info->fb_bpp = 32;

    return EFI_SUCCESS;
}

/*
 * Load the kernel ELF executable from the ESP.
 *
 * Parses the ELF64 program headers, loads each PT_LOAD segment at its
 * physical address (the kernel is linked at 1MB with identity mapping),
 * zeroes the .bss tail, and reports the real ELF entry point.
 */
EFI_STATUS
LoadKernel(EFI_HANDLE ImageHandle, UINT64 *entry_point)
{
    EFI_STATUS status;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root, *kernel_file;
    EFI_FILE_INFO *file_info;
    UINTN info_size;
    UINT64 kernel_size;
    Elf64Ehdr ehdr;
    Elf64Phdr phdrs[MAX_PHDRS];
    UINTN i;

    /* Prefer the volume this bootloader was loaded from; only fall back
     * to an arbitrary SimpleFileSystem volume if that fails. */
    {
        EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;

        status = BS->HandleProtocol(ImageHandle, &li_guid, (void **)&loaded_image);
        if (!EFI_ERROR(status) && loaded_image->DeviceHandle) {
            BS->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, (void **)&fs);
        }
    }
    if (!fs) {
        status = BS->LocateProtocol(&fs_guid, NULL, (void **)&fs);
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

    if (kernel_size < sizeof(Elf64Ehdr)) {
        Print(L"[FAIL] kernel.bin too small (%d bytes) - not an ELF\n",
              (UINT32)kernel_size);
        kernel_file->Close(kernel_file);
        root->Close(root);
        return EFI_LOAD_ERROR;
    }

    /* Read and validate the ELF header */
    {
        UINTN read_size = sizeof(ehdr);
        status = kernel_file->Read(kernel_file, &read_size, &ehdr);
        if (EFI_ERROR(status) || read_size != sizeof(ehdr)) {
            kernel_file->Close(kernel_file);
            root->Close(root);
            return EFI_LOAD_ERROR;
        }
    }

    if (ehdr.e_ident[EI_MAG0] != 0x7F || ehdr.e_ident[EI_MAG1] != 'E' ||
        ehdr.e_ident[EI_MAG2] != 'L' || ehdr.e_ident[EI_MAG3] != 'F' ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_type != ET_EXEC ||
        ehdr.e_machine != OJJY_EXPECTED_MACHINE) {
        Print(L"[FAIL] kernel.bin is not a static ELF64 executable for this architecture\n");
        kernel_file->Close(kernel_file);
        root->Close(root);
        return EFI_LOAD_ERROR;
    }

    if (ehdr.e_phnum == 0 || ehdr.e_phnum > MAX_PHDRS) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return EFI_UNSUPPORTED;
    }

    /* Read the program headers */
    status = kernel_file->SetPosition(kernel_file, ehdr.e_phoff);
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    {
        UINTN read_size = (UINTN)ehdr.e_phentsize * ehdr.e_phnum;
        if (read_size > sizeof(phdrs)) {
            kernel_file->Close(kernel_file);
            root->Close(root);
            return EFI_UNSUPPORTED;
        }
        status = kernel_file->Read(kernel_file, &read_size, phdrs);
        if (EFI_ERROR(status) || read_size != (UINTN)ehdr.e_phentsize * ehdr.e_phnum) {
            kernel_file->Close(kernel_file);
            root->Close(root);
            return EFI_LOAD_ERROR;
        }
    }

    /* Load each PT_LOAD segment at its physical address */
    for (i = 0; i < ehdr.e_phnum; i++) {
        Elf64Phdr *ph = &phdrs[i];
        EFI_PHYSICAL_ADDRESS addr;
        UINTN pages;

        if (ph->p_type != PT_LOAD) continue;

        /* Physical address (kernel is identity-mapped: p_paddr == p_vaddr) */
        UINT64 seg_start = ph->p_paddr & ~0xFFFULL;
        UINT64 seg_end = (ph->p_paddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;
        UINT64 seg_span = seg_end - seg_start;

        if (seg_span == 0) continue;
        pages = (UINTN)(seg_span / 4096);
        addr = seg_start;

        /* Allocate the exact address so the segment lands where it was linked.
         * EfiLoaderData pages are excluded from the kernel's free memory pool. */
        status = BS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr);
        if (EFI_ERROR(status)) {
            Print(L"[FAIL] Could not allocate kernel segment at 0x%lx (%d pages): %r\n",
                  (UINT64)seg_start, (UINT32)pages, status);
            kernel_file->Close(kernel_file);
            root->Close(root);
            return status;
        }

        /* Zero the full memory footprint (covers .bss beyond p_filesz) */
        SetMem((void *)(UINTN)seg_start, (UINTN)seg_span, 0);

        /* Read the file-backed portion */
        if (ph->p_filesz > 0) {
            status = kernel_file->SetPosition(kernel_file, ph->p_offset);
            if (EFI_ERROR(status)) {
                kernel_file->Close(kernel_file);
                root->Close(root);
                return status;
            }

            UINTN read_size = (UINTN)ph->p_filesz;
            status = kernel_file->Read(kernel_file, &read_size,
                                       (void *)(UINTN)ph->p_paddr);
            if (EFI_ERROR(status) || read_size != (UINTN)ph->p_filesz) {
                Print(L"[FAIL] Short read loading kernel segment\n");
                kernel_file->Close(kernel_file);
                root->Close(root);
                return EFI_LOAD_ERROR;
            }
        }
    }

    kernel_file->Close(kernel_file);
    root->Close(root);

    /* Entry point comes from the ELF header */
    *entry_point = ehdr.e_entry;

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
    mmap_size += 4 * desc_size;
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
 * Find ACPI RSDP from UEFI configuration tables.
 * Two passes: prefer an ACPI 2.0 table, fall back to ACPI 1.0.
 */
UINT64
FindRSDP(void)
{
    EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;
    EFI_GUID acpi10_guid = ACPI_TABLE_GUID;
    UINTN i;

    /* Pass 1: ACPI 2.0 */
    for (i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];
        if (CompareGuid(&table->VendorGuid, &acpi20_guid) == 0) {
            return (UINT64)table->VendorTable;
        }
    }

    /* Pass 2: ACPI 1.0 */
    for (i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];
        if (CompareGuid(&table->VendorGuid, &acpi10_guid) == 0) {
            return (UINT64)table->VendorTable;
        }
    }

    return 0;
}
