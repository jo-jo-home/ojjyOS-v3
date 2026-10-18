/*
 * ojjyOS v3 Kernel - Device register access (aarch64)
 *
 * Every MMIO access goes through these helpers: one plain LDR/STR with a
 * base register and no writeback. Compiler-chosen forms such as LDP/STP
 * or post-increment addressing carry no instruction syndrome (ISV=0), and
 * hardware virtualization (Apple HVF) cannot emulate them, so QEMU aborts
 * with assert(isv). Plain volatile pointer loops can produce exactly those.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_ARCH_MMIO_H
#define _OJJY_ARCH_MMIO_H

#include "types.h"

#if defined(OJJY_HOST_TEST) || !defined(__aarch64__)
static inline uint32_t mmio_read32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void mmio_write32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint16_t mmio_read16(uintptr_t a) { return *(volatile uint16_t *)a; }
static inline void mmio_write16(uintptr_t a, uint16_t v) { *(volatile uint16_t *)a = v; }
static inline uint8_t mmio_read8(uintptr_t a) { return *(volatile uint8_t *)a; }
static inline void mmio_write8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }
#else
static inline uint32_t mmio_read32(uintptr_t a)
{
    uint32_t v;
    __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(a) : "memory");
    return v;
}
static inline void mmio_write32(uintptr_t a, uint32_t v)
{
    __asm__ volatile("str %w0, [%1]" : : "r"(v), "r"(a) : "memory");
}
static inline uint16_t mmio_read16(uintptr_t a)
{
    uint16_t v;
    __asm__ volatile("ldrh %w0, [%1]" : "=r"(v) : "r"(a) : "memory");
    return v;
}
static inline void mmio_write16(uintptr_t a, uint16_t v)
{
    __asm__ volatile("strh %w0, [%1]" : : "r"((uint32_t)v), "r"(a) : "memory");
}
static inline uint8_t mmio_read8(uintptr_t a)
{
    uint32_t v;
    __asm__ volatile("ldrb %w0, [%1]" : "=r"(v) : "r"(a) : "memory");
    return (uint8_t)v;
}
static inline void mmio_write8(uintptr_t a, uint8_t v)
{
    __asm__ volatile("strb %w0, [%1]" : : "r"((uint32_t)v), "r"(a) : "memory");
}
#endif

#endif /* _OJJY_ARCH_MMIO_H */
