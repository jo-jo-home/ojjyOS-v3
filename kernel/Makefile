# ojjyOS v3 Kernel Makefile
#
# Produces kernel.bin — an ELF64 executable loaded by the ojjyOS UEFI
# bootloader, which parses the program headers and jumps to the ELF entry.
#
# Dual-architecture build:
#   make ARCH=x86_64   (default)
#   make ARCH=aarch64  (Apple Silicon / ARM64 QEMU -M virt)

ARCH ?= x86_64

CC = clang
LD := $(shell command -v ld.lld 2>/dev/null || \
        ls /opt/homebrew/opt/lld/bin/ld.lld /opt/homebrew/opt/llvm/bin/ld.lld /usr/local/opt/lld/bin/ld.lld 2>/dev/null | head -n 1)
ifeq ($(LD),)
LD = ld.lld
endif
NASM = nasm

COMMON_CFLAGS = -ffreestanding \
                -fno-stack-protector \
                -fno-stack-check \
                -fno-pie \
                -fno-pic \
                -Wall \
                -Wextra \
                -Wno-unused-parameter \
                -O2 \
                -Isrc

COMMON_C_SOURCES = src/main.c \
                   src/system.c \
                   src/users.c \
                   src/proc.c \
                   src/string.c \
                   src/memory.c \
                   src/framebuffer.c \
                   src/font.c \
                   src/console.c \
                   src/panic.c \
                   src/serial_core.c \
                   src/drivers/driver.c \
                   src/drivers/input.c \
                   src/drivers/diagnostics.c \
                   src/fs/vfs.c \
                   src/fs/ojfs.c \
                   src/fs/ramfs.c \
                   src/fs/bundle.c \
                   src/fs/persist.c \
                   src/ui/compositor.c \
                   src/ui/services.c \
                   src/ui/theme.c

ifeq ($(ARCH),aarch64)
TARGET_TRIPLE = aarch64-unknown-none-elf
CFLAGS = --target=$(TARGET_TRIPLE) $(COMMON_CFLAGS) -mgeneral-regs-only
LINKER_SCRIPT = linker-aarch64.ld
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -static -z max-page-size=0x1000

ARCH_C_SOURCES = src/arch/aarch64/arch64.c \
                 src/arch/aarch64/gicv2.c \
                 src/arch/aarch64/timer_a64.c \
                 src/arch/aarch64/pl011.c \
                 src/arch/aarch64/pl031.c \
                 src/arch/aarch64/virtio.c \
                 src/arch/aarch64/virtio_input.c \
                 src/arch/aarch64/virtio_blk.c \
                 src/arch/aarch64/psci.c

ASM_SOURCES = src/arch/aarch64/entry.S
ASM_OBJECTS = $(ASM_SOURCES:.S=.o)
else
TARGET_TRIPLE = x86_64-unknown-none-elf
CFLAGS = --target=$(TARGET_TRIPLE) $(COMMON_CFLAGS) \
         -mno-red-zone \
         -mno-mmx \
         -mno-sse \
         -mno-sse2 \
         -mcmodel=kernel
LINKER_SCRIPT = linker.ld
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -static -z max-page-size=0x1000
NASMFLAGS = -f elf64

ARCH_C_SOURCES = src/gdt.c \
                 src/idt.c \
                 src/paging.c \
                 src/timer.c \
                 src/serial.c \
                 src/drivers/ps2_keyboard.c \
                 src/drivers/ps2_mouse.c \
                 src/drivers/ata.c \
                 src/drivers/rtc.c \
                 src/drivers/block_cache.c \
                 src/drivers/acpi.c \
                 src/drivers/blockdev_x86.c

ASM_SOURCES = src/entry.asm
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
endif

C_SOURCES = $(COMMON_C_SOURCES) $(ARCH_C_SOURCES)
C_OBJECTS = $(C_SOURCES:.c=.o)
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# All objects across both arches (for clean)
ALL_CLEAN_OBJECTS = $(COMMON_C_SOURCES:.c=.o) \
                    src/gdt.o src/idt.o src/paging.o src/timer.o src/serial.o \
                    src/drivers/ps2_keyboard.o src/drivers/ps2_mouse.o \
                    src/drivers/ata.o src/drivers/rtc.o src/drivers/block_cache.o \
                    src/drivers/acpi.o src/drivers/blockdev_x86.o src/entry.o \
                    src/arch/aarch64/arch64.o src/arch/aarch64/gicv2.o \
                    src/arch/aarch64/timer_a64.o src/arch/aarch64/pl011.o \
                    src/arch/aarch64/pl031.o src/arch/aarch64/virtio.o \
                    src/arch/aarch64/virtio_input.o src/arch/aarch64/virtio_blk.o \
                    src/arch/aarch64/psci.o src/arch/aarch64/entry.o
ALL_CLEAN_DEPS = $(ALL_CLEAN_OBJECTS:.o=.d)

# Output
TARGET = kernel.bin

# Header dependency files
DEPS = $(C_OBJECTS:.o=.d)

.PHONY: all clean objs

all: $(TARGET)

objs: $(OBJECTS)

# Link kernel
$(TARGET): $(OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Assemble x86_64 NASM files
%.o: %.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

# Assemble aarch64 GNU assembly files via clang
%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(ALL_CLEAN_OBJECTS) $(ALL_CLEAN_DEPS) $(TARGET)

# Pull in header dependencies (ignore missing files on first build)
-include $(DEPS)
