#ifndef FUNNYOS_KERNEL_IO_H
#define FUNNYOS_KERNEL_IO_H

#include "../common/types.h"

static inline void io_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t io_in16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t io_in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_out16(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out32(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void cpu_pause(void)
{
    __asm__ volatile("pause");
}

static inline uint64_t io_rdtsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (uint64_t)low | ((uint64_t)high << 32u);
}

static inline uint64_t io_rdmsr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (uint64_t)low | ((uint64_t)high << 32u);
}

static inline void io_wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32u)));
}

#endif
