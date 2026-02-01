/*
 * ojjyOS v3 Kernel - Basic Types
 *
 * Standard integer types for freestanding environment
 */

#ifndef _OJJY_TYPES_H
#define _OJJY_TYPES_H

/* Fixed-width integer types */
typedef unsigned char       uint8_t;
typedef signed char         int8_t;
typedef unsigned short      uint16_t;
typedef signed short        int16_t;
typedef unsigned int        uint32_t;
typedef signed int          int32_t;
typedef unsigned long long  uint64_t;
typedef signed long long    int64_t;

/* Size types */
typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;

/* Boolean */
typedef _Bool               bool;
#define true                1
#define false               0

/* NULL pointer */
#define NULL                ((void *)0)

/* Useful macros */
#define UNUSED(x)           ((void)(x))
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define ALIGN_UP(x, a)      (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))

/* Page size */
#define PAGE_SIZE           4096
#define PAGE_MASK           (PAGE_SIZE - 1)

/* Packed attribute */
#define PACKED              __attribute__((packed))

/* Inline assembly helpers */
#define barrier()           __asm__ volatile("" ::: "memory")

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cli(void)
{
    __asm__ volatile("cli");
}

static inline void sti(void)
{
    __asm__ volatile("sti");
}

static inline void hlt(void)
{
    __asm__ volatile("hlt");
}

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

#endif /* _OJJY_TYPES_H */
