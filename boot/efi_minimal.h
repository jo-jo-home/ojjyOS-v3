/*
 * ojjyOS v3 - Self-Contained UEFI Definitions (for macOS / freestanding clang builds)
 * Created by Jonas Lee
 *
 * Allows building BOOTAA64.EFI and BOOTX64.EFI directly on macOS with clang + lld-link
 * without requiring Linux's /usr/include/efi (gnu-efi).
 */

#ifndef _OJJY_EFI_MINIMAL_H
#define _OJJY_EFI_MINIMAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t   BOOLEAN;
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int64_t   INTN;
typedef uint64_t  UINTN;
typedef uint16_t  CHAR16;
typedef void      VOID;
typedef UINTN     EFI_STATUS;
typedef VOID     *EFI_HANDLE;
typedef VOID     *EFI_EVENT;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINT64    EFI_VIRTUAL_ADDRESS;

#if defined(__x86_64__)
  #define EFIAPI __attribute__((ms_abi))
#else
  #define EFIAPI
#endif

#ifndef TRUE
  #define TRUE  ((BOOLEAN)1)
  #define FALSE ((BOOLEAN)0)
#endif

#define EFI_ERROR_BIT           (1ULL << 63)
#define EFI_ERROR(s)            (((s) & EFI_ERROR_BIT) != 0)
#define EFI_SUCCESS             0ULL
#define EFI_LOAD_ERROR          (EFI_ERROR_BIT | 1ULL)
#define EFI_INVALID_PARAMETER   (EFI_ERROR_BIT | 2ULL)
#define EFI_UNSUPPORTED         (EFI_ERROR_BIT | 3ULL)
#define EFI_BUFFER_TOO_SMALL    (EFI_ERROR_BIT | 5ULL)
#define EFI_NOT_FOUND           (EFI_ERROR_BIT | 14ULL)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* Memory types & allocation */
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32               Type;
    UINT32               Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Console I/O */
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
    struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET    Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT          WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET        Reset;
    EFI_TEXT_STRING       OutputString;
    VOID                 *TestString;
    VOID                 *QueryMode;
    VOID                 *SetMode;
    VOID                 *SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    VOID                 *SetCursorPosition;
    VOID                 *EnableCursor;
    VOID                 *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Runtime Services */
typedef enum {
    EfiResetCold,
    EfiResetWarm,
    EfiResetShutdown
} EFI_RESET_TYPE;

typedef VOID (EFIAPI *EFI_RESET_SYSTEM)(
    EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    VOID            *GetTime;
    VOID            *SetTime;
    VOID            *GetWakeupTime;
    VOID            *SetWakeupTime;
    VOID            *SetVirtualAddressMap;
    VOID            *ConvertPointer;
    VOID            *GetVariable;
    VOID            *GetNextVariableName;
    VOID            *SetVariable;
    VOID            *GetNextHighMonotonicCount;
    EFI_RESET_SYSTEM ResetSystem;
} EFI_RUNTIME_SERVICES;

/* Boot Services */
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey,
    UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(
    UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle, UINTN MapKey);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(
    UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol, VOID *Registration, VOID **Interface);

typedef struct {
    EFI_TABLE_HEADER       Hdr;
    VOID                  *RaiseTPL;
    VOID                  *RestoreTPL;
    EFI_ALLOCATE_PAGES     AllocatePages;
    EFI_FREE_PAGES         FreePages;
    EFI_GET_MEMORY_MAP     GetMemoryMap;
    EFI_ALLOCATE_POOL      AllocatePool;
    EFI_FREE_POOL          FreePool;
    VOID                  *CreateEvent;
    VOID                  *SetTimer;
    EFI_WAIT_FOR_EVENT     WaitForEvent;
    VOID                  *SignalEvent;
    VOID                  *CloseEvent;
    VOID                  *CheckEvent;
    VOID                  *InstallProtocolInterface;
    VOID                  *ReinstallProtocolInterface;
    VOID                  *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL    HandleProtocol;
    VOID                  *Reserved;
    VOID                  *RegisterProtocolNotify;
    VOID                  *LocateHandle;
    VOID                  *LocateDevicePath;
    VOID                  *InstallConfigurationTable;
    VOID                  *LoadImage;
    VOID                  *StartImage;
    VOID                  *Exit;
    VOID                  *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    VOID                  *GetNextMonotonicCount;
    VOID                  *Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    VOID                  *ConnectController;
    VOID                  *DisconnectController;
    VOID                  *OpenProtocol;
    VOID                  *CloseProtocol;
    VOID                  *OpenProtocolInformation;
    VOID                  *ProtocolsPerHandle;
    VOID                  *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL    LocateProtocol;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_GUID VendorGuid;
    VOID    *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    CHAR16                          *FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    UINTN                            NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE         *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Graphics Output Protocol (GOP) */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                MaxMode;
    UINT32                                Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                 SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
    UINTN                                 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber,
    UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    VOID                                   *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE      *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* Loaded Image & Simple File System Protocols */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE        DeviceHandle;
    VOID             *FilePath;
    VOID             *Reserved;
    UINT32            LoadOptionsSize;
    VOID             *LoadOptions;
    VOID             *ImageBase;
    UINT64            ImageSize;
    EFI_MEMORY_TYPE   ImageCodeType;
    EFI_MEMORY_TYPE   ImageDataType;
    VOID             *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x0964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_INFO_ID \
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

typedef struct {
    UINT16 Year;
    UINT8  Month;
    UINT8  Day;
    UINT8  Hour;
    UINT8  Minute;
    UINT8  Second;
    UINT8  Pad1;
    UINT32 Nanosecond;
     int16_t TimeZone;
    UINT8  Daylight;
    UINT8  Pad2;
} EFI_TIME;

typedef struct {
    UINT64   Size;
    UINT64   FileSize;
    UINT64   PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64   Attribute;
    CHAR16   FileName[1];
} EFI_FILE_INFO;

struct _EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    struct _EFI_FILE_PROTOCOL *This, struct _EFI_FILE_PROTOCOL **NewHandle,
    CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(struct _EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    struct _EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
    struct _EFI_FILE_PROTOCOL *This, UINT64 Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    struct _EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
    UINTN *BufferSize, VOID *Buffer);

typedef struct _EFI_FILE_PROTOCOL {
    UINT64                Revision;
    EFI_FILE_OPEN         Open;
    EFI_FILE_CLOSE        Close;
    VOID                 *Delete;
    EFI_FILE_READ         Read;
    EFI_STATUS (EFIAPI *Write)(struct _EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
    VOID                 *GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO     GetInfo;
    VOID                 *SetInfo;
    EFI_STATUS (EFIAPI *Flush)(struct _EFI_FILE_PROTOCOL *This);
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                                      Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* ACPI Table GUIDs */
#define ACPI_TABLE_GUID \
    { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define ACPI_20_TABLE_GUID \
    { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

/* Global pointers matching gnu-efi conventions */
static EFI_SYSTEM_TABLE     *ST = NULL;
static EFI_BOOT_SERVICES    *BS = NULL;
static EFI_RUNTIME_SERVICES *RT = NULL;

static inline VOID InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    ST = SystemTable;
    BS = SystemTable->BootServices;
    RT = SystemTable->RuntimeServices;
}

static inline VOID SetMem(VOID *Buffer, UINTN Size, UINT8 Value)
{
    UINT8 *p = (UINT8 *)Buffer;
    for (UINTN i = 0; i < Size; i++) p[i] = Value;
}

/* Required by clang when compiling freestanding struct assignments */
void *memcpy(void *dst, const void *src, size_t n)
{
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    UINT8 *d = (UINT8 *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (UINT8)c;
    return dst;
}

static inline INTN CompareGuid(EFI_GUID *Guid1, EFI_GUID *Guid2)
{
    const UINT8 *a = (const UINT8 *)Guid1;
    const UINT8 *b = (const UINT8 *)Guid2;
    for (UINTN i = 0; i < sizeof(EFI_GUID); i++) {
        if (a[i] != b[i]) return (INTN)a[i] - (INTN)b[i];
    }
    return 0;
}

static inline EFI_STATUS WaitForSingleEvent(EFI_EVENT Event, UINT64 Timeout)
{
    (void)Timeout;
    UINTN idx = 0;
    return BS->WaitForEvent(1, &Event, &idx);
}

/* Everything printed is also kept (ASCII) so the bootloader can save a boot
 * log on machines whose console we cannot see (Apple Virtualization). */
#define EFI_LOG_MAX 49152
static char efi_log[EFI_LOG_MAX];
static UINTN efi_log_len;

static inline void efi_out(CHAR16 *s)
{
    for (UINTN i = 0; s[i] && efi_log_len < EFI_LOG_MAX; i++)
        if (s[i] != L'\r') efi_log[efi_log_len++] = s[i] < 128 ? (char)s[i] : '?';
    ST->ConOut->OutputString(ST->ConOut, s);
}

static inline void efi_puts_u16(const CHAR16 *s)
{
    if (!ST || !ST->ConOut) return;
    CHAR16 buf[128];
    UINTN bi = 0;
    for (UINTN i = 0; s[i] != 0; i++) {
        if (s[i] == L'\n') {
            buf[bi++] = L'\r';
            if (bi >= 126) { buf[bi] = 0; efi_out(buf); bi = 0; }
        }
        buf[bi++] = s[i];
        if (bi >= 126) { buf[bi] = 0; efi_out(buf); bi = 0; }
    }
    if (bi > 0) {
        buf[bi] = 0;
        efi_out(buf);
    }
}

static inline void efi_print_uint(UINT64 v, UINT32 base)
{
    CHAR16 tmp[32];
    int pos = 0;
    if (v == 0) {
        tmp[pos++] = L'0';
    } else {
        while (v > 0 && pos < 30) {
            UINT32 d = (UINT32)(v % base);
            tmp[pos++] = (CHAR16)(d < 10 ? (L'0' + d) : (L'a' + (d - 10)));
            v /= base;
        }
    }
    CHAR16 out[32];
    for (int i = 0; i < pos; i++) out[i] = tmp[pos - 1 - i];
    out[pos] = 0;
    efi_puts_u16(out);
}

static inline UINTN Print(const CHAR16 *fmt, ...)
{
    if (!ST || !ST->ConOut || !fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    CHAR16 ch[2] = { 0, 0 };
    for (UINTN i = 0; fmt[i] != 0; i++) {
        if (fmt[i] != L'%') {
            if (fmt[i] == L'\n') { ch[0] = L'\r'; efi_out(ch); }
            ch[0] = fmt[i];
            efi_out(ch);
            continue;
        }
        i++;
        if (fmt[i] == L'l') i++;
        switch (fmt[i]) {
        case L's': {
            const CHAR16 *s = va_arg(ap, const CHAR16 *);
            if (s) efi_puts_u16(s);
            break;
        }
        case L'd':
        case L'u': {
            UINT64 v = va_arg(ap, UINT64);
            efi_print_uint(v, 10);
            break;
        }
        case L'x':
        case L'r': {
            UINT64 v = va_arg(ap, UINT64);
            efi_print_uint(v, 16);
            break;
        }
        default:
            ch[0] = fmt[i];
            efi_out(ch);
            break;
        }
    }
    va_end(ap);
    return 0;
}

#endif /* _OJJY_EFI_MINIMAL_H */
