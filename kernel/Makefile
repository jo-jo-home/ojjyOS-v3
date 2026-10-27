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
                -Werror=return-type \
                -Wno-unused-parameter \
                -O2 \
                -Isrc

COMMON_C_SOURCES = src/main.c \
                   src/system.c \
                   src/users.c \
                   src/proc.c \
                   src/user.c \
                   src/user_image.c \
                   src/string.c \
                   src/gfx.c \
                   src/image/jpeg.c \
                   src/image/wallpack.c \
                   src/cursor_image.c \
                   src/uifont.c \
                   src/uifont_data.c \
                   src/font2x.c \
                   src/ui/wallpaper.c \
                   src/ui/icons_data.c \
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
                   src/ui/theme.c \
                   src/random.c \
                   src/crypto/sha256.c \
                   src/crypto/sha512.c \
                   src/crypto/bignum.c \
                   src/crypto/rsa.c \
                   src/crypto/ecdsa.c \
                   src/crypto/x25519.c \
                   src/crypto/aes_gcm.c \
                   src/crypto/chacha_poly.c \
                   src/net/net.c \
                   src/net/tcp.c \
                   src/net/tls.c \
                   src/web/url.c \
                   src/web/http.c \
                   src/web/dom.c \
                   src/web/css.c \
                   src/web/layout.c \
                   src/web/paint.c \
                   src/webfont.c \
                   src/webfont_data.c \
                   src/image/inflate.c \
                   src/image/png.c \
                   src/image/gif.c \
                   src/image/webp.c \
                   src/image/svg.c \
                   src/image/image.c \
                   src/ui/browser.c \
                   src/ui/browser_ui.c \
                   src/ui/browser_profile.c \
                   src/ui/browser_pages.c \
                   src/ui/netcmd.c \
                   src/ui/calcmath.c \
                   src/ui/notes.c \
                   src/ui/textedit.c

ifeq ($(ARCH),aarch64)
TARGET_TRIPLE = aarch64-unknown-none-elf
CFLAGS = --target=$(TARGET_TRIPLE) $(COMMON_CFLAGS) -mgeneral-regs-only
LINKER_SCRIPT = linker-aarch64.ld
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -static -z max-page-size=0x1000

ARCH_C_SOURCES = src/arch/aarch64/arch64.c \
                 src/arch/aarch64/user_arch.c \
                 src/arch/aarch64/gicv2.c \
                 src/arch/aarch64/timer_a64.c \
                 src/arch/aarch64/pl011.c \
                 src/arch/aarch64/pl031.c \
                 src/arch/aarch64/platform.c \
                 src/arch/aarch64/pci.c \
                 src/arch/aarch64/virtio.c \
                 src/arch/aarch64/virtio_input.c \
                 src/arch/aarch64/virtio_blk.c \
                 src/arch/aarch64/virtio_net.c \
                 src/arch/aarch64/virtio_rng.c \
                 src/arch/aarch64/virtio_console.c \
                  src/arch/aarch64/virtio_gpu.c \
                  src/arch/aarch64/xhci.c \
                  src/arch/aarch64/usb_hid.c \
                 src/arch/aarch64/psci.c

ASM_SOURCES = src/arch/aarch64/entry.S src/arch/aarch64/user_entry.S
ASM_OBJECTS = $(addprefix $(OBJ_DIR)/,$(ASM_SOURCES:.S=.o))
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
                 src/user_unsupported.c \
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
                  src/drivers/acpi_aml.c \
                 src/drivers/blockdev_x86.c

ASM_SOURCES = src/entry.asm
ASM_OBJECTS = $(addprefix $(OBJ_DIR)/,$(ASM_SOURCES:.asm=.o))
endif

C_SOURCES = $(COMMON_C_SOURCES) $(ARCH_C_SOURCES)
# Build products live under build/, never next to the sources (QA-22).
OBJ_DIR = ../build/obj-$(ARCH)
C_OBJECTS = $(addprefix $(OBJ_DIR)/,$(C_SOURCES:.c=.o))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

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
$(OBJ_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Assemble x86_64 NASM files
$(OBJ_DIR)/src/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) -o $@ $<

# Assemble aarch64 GNU assembly files via clang
$(OBJ_DIR)/src/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf ../build/obj-aarch64 ../build/obj-x86_64
	rm -f $(TARGET)
	rm -f $(ALL_CLEAN_OBJECTS) $(ALL_CLEAN_DEPS)

# Pull in header dependencies (ignore missing files on first build)
-include $(DEPS)

# Legacy in-src objects from before the build moved under build/ (QA-22):
# clean still removes them, so an older checkout tidies up too.
ALL_CLEAN_OBJECTS = $(COMMON_C_SOURCES:.c=.o) \
                    src/user_unsupported.o src/arch/aarch64/user_arch.o src/arch/aarch64/user_entry.o \
                    src/gdt.o src/idt.o src/paging.o src/timer.o src/serial.o \
                    src/drivers/ps2_keyboard.o src/drivers/ps2_mouse.o \
                    src/drivers/ata.o src/drivers/rtc.o src/drivers/block_cache.o \
                    src/drivers/acpi.o src/drivers/acpi_aml.o src/drivers/blockdev_x86.o src/entry.o \
                    src/arch/aarch64/arch64.o src/arch/aarch64/gicv2.o \
                    src/arch/aarch64/timer_a64.o src/arch/aarch64/pl011.o \
                    src/arch/aarch64/pl031.o src/arch/aarch64/virtio.o \
                    src/arch/aarch64/virtio_input.o src/arch/aarch64/virtio_blk.o \
                    src/arch/aarch64/virtio_net.o src/arch/aarch64/virtio_rng.o \
                     src/arch/aarch64/virtio_gpu.o \
                     src/arch/aarch64/xhci.o \
                     src/arch/aarch64/usb_hid.o \
                    src/arch/aarch64/virtio_console.o \
                    src/arch/aarch64/platform.o src/arch/aarch64/pci.o \
                    src/arch/aarch64/psci.o src/arch/aarch64/entry.o
ALL_CLEAN_DEPS = $(ALL_CLEAN_OBJECTS:.o=.d)
