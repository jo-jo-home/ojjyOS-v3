# ojjyOS v3 Kernel Makefile

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
         -Werror \
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

.PHONY: all clean

all: $(TARGET)

# Link kernel
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble ASM files
%.o: %.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
 	rm -f src/*.o src/drivers/*.o src/fs/*.o src/ui/*.o
