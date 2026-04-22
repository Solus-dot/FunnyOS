#ifndef FUNNYOS_KERNEL_ACPI_H
#define FUNNYOS_KERNEL_ACPI_H

#include "../common/bootinfo.h"
#include "../common/types.h"

typedef struct AcpiPlatformInfo {
    bool present;
    uintptr_t rsdp;
    uintptr_t sdt_root;
    uintptr_t madt;
    uintptr_t fadt;
    uint32_t lapic_address;
    uint32_t ioapic_address;
    uint32_t ioapic_gsi_base;
    uint8_t ioapic_id;
} AcpiPlatformInfo;

bool acpi_init(const BootInfo* boot_info);
const AcpiPlatformInfo* acpi_platform_info(void);

#endif
