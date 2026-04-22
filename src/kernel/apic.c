#include "apic.h"
#include "io.h"
#include "paging.h"

#define IA32_APIC_BASE_MSR 0x1Bu
#define IA32_APIC_BASE_ENABLE (1ull << 11)
#define IA32_APIC_BASE_BSP (1ull << 8)
#define IA32_APIC_BASE_ADDR_MASK 0xFFFFF000ull

#define LAPIC_REG_ID 0x020u
#define LAPIC_REG_EOI 0x0B0u
#define LAPIC_REG_SPURIOUS 0x0F0u
#define LAPIC_REG_LVT_TIMER 0x320u
#define LAPIC_SPURIOUS_ENABLE 0x100u
#define LAPIC_TIMER_MASKED 0x00010000u

static volatile uint32_t* g_lapic = NULL;
static uint32_t g_lapic_hz = 0u;

static inline uint32_t lapic_read(uint32_t offset)
{
    return g_lapic[offset / sizeof(uint32_t)];
}

static inline void lapic_write(uint32_t offset, uint32_t value)
{
    g_lapic[offset / sizeof(uint32_t)] = value;
}

bool apic_init(const AcpiPlatformInfo* acpi_info)
{
    uint64_t apic_base;
    uintptr_t lapic_base;

    if (acpi_info == NULL || !acpi_info->present || acpi_info->lapic_address == 0u)
        return false;

    apic_base = io_rdmsr(IA32_APIC_BASE_MSR);
    lapic_base = (uintptr_t)(apic_base & IA32_APIC_BASE_ADDR_MASK);
    if (lapic_base == 0u)
        lapic_base = (uintptr_t)acpi_info->lapic_address;

    if (!paging_map_range(lapic_base, lapic_base, 4096u, true, false))
        return false;
    if ((uintptr_t)acpi_info->lapic_address != lapic_base) {
        (void)paging_map_range((uintptr_t)acpi_info->lapic_address, (uintptr_t)acpi_info->lapic_address, 4096u, true, false);
    }
    if (acpi_info->ioapic_address != 0u) {
        (void)paging_map_range((uintptr_t)acpi_info->ioapic_address, (uintptr_t)acpi_info->ioapic_address, 4096u, true, false);
    }

    apic_base &= ~IA32_APIC_BASE_ADDR_MASK;
    apic_base |= ((uint64_t)lapic_base & IA32_APIC_BASE_ADDR_MASK);
    apic_base |= IA32_APIC_BASE_ENABLE;
    io_wrmsr(IA32_APIC_BASE_MSR, apic_base);

    g_lapic = (volatile uint32_t*)lapic_base;

    lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | LAPIC_SPURIOUS_ENABLE | 0xFFu);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_REG_EOI, 0u);
    return true;
}

uint32_t apic_lapic_id(void)
{
    if (g_lapic == NULL)
        return 0u;
    return lapic_read(LAPIC_REG_ID) >> 24u;
}

uint32_t apic_lapic_hz(void)
{
    return g_lapic_hz;
}

void apic_set_lapic_hz(uint32_t hz)
{
    g_lapic_hz = hz;
}
