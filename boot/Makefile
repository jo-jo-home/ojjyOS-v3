# ojjyOS v3 UEFI Bootloader Makefile
#
# Supports both x86_64 (BOOTX64.EFI) and aarch64 (BOOTAA64.EFI).
# Works natively on macOS (clang + lld-link via `brew install lld`)
# AND on Debian/Ubuntu Linux (gnu-efi OR clang + lld-link).

ARCH ?= $(shell uname -m)
ifeq ($(ARCH),arm64)
  override ARCH := aarch64
endif

EFI_INC = /usr/include/efi
EFI_LIB = /usr/lib

HAVE_GNUEFI := $(wildcard $(EFI_INC)/efi.h)

LLD_LINK := $(shell command -v lld-link 2>/dev/null || \
              ls /opt/homebrew/opt/lld/bin/lld-link /opt/homebrew/opt/llvm/bin/lld-link /usr/local/opt/lld/bin/lld-link 2>/dev/null | head -n 1)

TARGET = ojjy-boot.efi

.PHONY: all clean

ifeq ($(HAVE_GNUEFI),)
# Standalone PE/COFF UEFI build (macOS / clang + lld-link)
ifeq ($(ARCH),aarch64)
  PE_TARGET = aarch64-unknown-windows
else
  PE_TARGET = x86_64-unknown-windows
endif

CC = clang
CFLAGS = --target=$(PE_TARGET) -DOJJY_STANDALONE_EFI \
         -ffreestanding -fno-stack-protector -fno-stack-check \
         -fshort-wchar -mno-stack-arg-probe -Wall -Wextra -O2 -c
OBJ = ojjy-boot.obj

all: $(TARGET)

$(OBJ): ojjy-boot.c efi_minimal.h display_mode.h
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET): $(OBJ)
	@if [ -z "$(LLD_LINK)" ]; then \
		echo "Error: lld-link not found. Install on macOS with: brew install lld"; \
		exit 1; \
	fi
	"$(LLD_LINK)" /subsystem:efi_application /entry:efi_main /nodefaultlib /dll /out:$@ $<

clean:
	rm -f *.o *.obj *.so *.efi

else
# Linux gnu-efi build
ifeq ($(ARCH),aarch64)
  CROSS_PREFIX ?=
  CC = $(CROSS_PREFIX)gcc
  LD = $(CROSS_PREFIX)ld
  OBJCOPY = $(CROSS_PREFIX)objcopy
  EFI_CRT = $(EFI_LIB)/crt0-efi-aarch64.o
  EFI_LDS = $(EFI_LIB)/elf_aarch64_efi.lds
  CFLAGS = -I$(EFI_INC) -I$(EFI_INC)/aarch64 \
           -ffreestanding -fno-stack-protector -fno-stack-check \
           -fshort-wchar -fpic \
           -Wall -Wextra -c
  OBJCOPY_FLAGS = -O binary \
                  -j .text -j .sdata -j .data -j .dynamic \
                  -j .dynsym -j .rel -j .rela -j .reloc
else
  CC = gcc
  LD = ld
  OBJCOPY = objcopy
  EFI_CRT = $(EFI_LIB)/crt0-efi-x86_64.o
  EFI_LDS = $(EFI_LIB)/elf_x86_64_efi.lds
  CFLAGS = -I$(EFI_INC) -I$(EFI_INC)/x86_64 \
           -DEFI_FUNCTION_WRAPPER \
           -ffreestanding -fno-stack-protector -fno-stack-check \
           -fshort-wchar -mno-red-zone \
           -Wall -Wextra -c
  OBJCOPY_FLAGS = -j .text -j .sdata -j .data -j .dynamic \
                  -j .dynsym -j .rel -j .rela -j .reloc \
                  --target=efi-app-x86_64
endif

LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
          -Bsymbolic -L $(EFI_LIB)
LIBS = -lefi -lgnuefi
OBJ = ojjy-boot.o

all: $(TARGET)

ojjy-boot.o: ojjy-boot.c display_mode.h
	$(CC) $(CFLAGS) -o $@ $<

ojjy-boot.so: $(OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $^ -o $@ $(LIBS)

$(TARGET): ojjy-boot.so
	$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@

clean:
	rm -f *.o *.obj *.so *.efi
endif
