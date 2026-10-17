# ojjyOS v3 UEFI Bootloader Makefile
#
# Supports both x86_64 (BOOTX64.EFI) and aarch64 (BOOTAA64.EFI)
# using Debian's gnu-efi package (build on Linux / Debian).

ARCH ?= $(shell uname -m)
ifeq ($(ARCH),arm64)
  override ARCH := aarch64
endif

# GNU-EFI paths (Debian package: gnu-efi)
EFI_INC = /usr/include/efi
EFI_LIB = /usr/lib

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

TARGET = ojjy-boot.efi
OBJ = ojjy-boot.o

.PHONY: all clean

all: $(TARGET)

ojjy-boot.o: ojjy-boot.c
	$(CC) $(CFLAGS) -o $@ $<

ojjy-boot.so: $(OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $^ -o $@ $(LIBS)

$(TARGET): ojjy-boot.so
	$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@

clean:
	rm -f *.o *.so *.efi
