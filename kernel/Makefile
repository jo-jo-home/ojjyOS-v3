# ojjyOS v3 Kernel Makefile
#
# Produces kernel.bin — an ELF64 executable loaded by the ojjyOS UEFI
# bootloader, which parses the program headers and jumps to the ELF entry.

CC = clang
LD = ld.lld
NASM = nasm

# Compiler flags for freestanding x86_64 kernel
CFLAGS = -target x86_64-unknown-none-elf \
         -ffreestanding \
         -fno-stack-protector \
         -fno-stack-check \
         -fno-pie \
         -fno-pic \
         -mno-red-zone \
         -mno-mmx \
         -mno-sse \
         -mno-sse2 \
         -mcmodel=kernel \
         -Wall \
         -Wextra \
         -Wno-unused-parameter \
         -O2 \
         -Isrc

# Linker flags
LDFLAGS = -T linker.ld \
          -nostdlib \
          -static \
          -z max-page-size=0x1000

# NASM flags
NASMFLAGS = -f elf64

# Source files
C_SOURCES = $(wildcard src/*.c) $(wildcard src/drivers/*.c) $(wildcard src/fs/*.c) $(wildcard src/ui/*.c)
ASM_SOURCES = $(wildcard src/*.asm)

# Object files (convert path and extension)
C_OBJECTS = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# Output
TARGET = kernel.bin

# Header dependency files
DEPS = $(C_OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

# Link kernel
$(TARGET): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Assemble ASM files
%.o: %.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET) $(DEPS)

# Pull in header dependencies (ignore missing files on first build)
-include $(DEPS)
