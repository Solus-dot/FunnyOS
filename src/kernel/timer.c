#include "timer.h"
#include "apic.h"
#include "io.h"

#define PIT_BASE_HZ 1193182ull
#define PIT_CH2_RELOAD 65535u

static uint64_t g_tsc_hz = 0u;

static bool timer_calibrate_tsc_from_pit(void)
{
    uint8_t port61;
    uint64_t tsc_start;
    uint64_t tsc_end;
    uint64_t tsc_delta;

    port61 = io_in8(0x61u);
    io_out8(0x61u, (uint8_t)((port61 | 0x01u) & (uint8_t)~0x02u));

    io_out8(0x43u, 0xB0u);
    io_out8(0x42u, (uint8_t)(PIT_CH2_RELOAD & 0xFFu));
    io_out8(0x42u, (uint8_t)((PIT_CH2_RELOAD >> 8u) & 0xFFu));

    tsc_start = io_rdtsc();
    while ((io_in8(0x61u) & 0x20u) == 0u)
        cpu_pause();
    tsc_end = io_rdtsc();

    io_out8(0x61u, port61);

    if (tsc_end <= tsc_start)
        return false;
    tsc_delta = tsc_end - tsc_start;
    g_tsc_hz = (tsc_delta * PIT_BASE_HZ) / PIT_CH2_RELOAD;
    return g_tsc_hz != 0u;
}

bool timer_init(void)
{
    if (!timer_calibrate_tsc_from_pit())
        return false;

    apic_set_lapic_hz((uint32_t)g_tsc_hz);
    return true;
}

uint64_t timer_tsc_hz(void)
{
    return g_tsc_hz;
}
