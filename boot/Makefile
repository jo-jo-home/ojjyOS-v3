# ojjyOS v3 UEFI Bootloader Makefile

# GNU-EFI paths (Debian package: gnu-efi)
EFI_INC = /usr/include/efi
EFI_LIB = /usr/lib
EFI_CRT = /usr/lib/crt0-efi-x86_64.o
EFI_LDS = /usr/lib/elf_x86_64_efi.lds

CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -I$(EFI_INC) -I$(EFI_INC)/x86_64 \
         -DEFI_FUNCTION_WRAPPER \
         -ffreestanding -fno-stack-protector -fno-stack-check \
         -fshort-wchar -mno-red-zone \
         -Wall -Wextra -c

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
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic \
	           -j .dynsym -j .rel -j .rela -j .reloc \
	           --target=efi-app-x86_64 $< $@

clean:
	rm -f *.o *.so *.efi
