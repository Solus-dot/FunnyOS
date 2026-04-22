#ifndef FUNNYOS_KERNEL_APIC_H
#define FUNNYOS_KERNEL_APIC_H

#include "../common/types.h"
#include "acpi.h"

bool apic_init(const AcpiPlatformInfo* acpi_info);
uint32_t apic_lapic_id(void);
uint32_t apic_lapic_hz(void);
void apic_set_lapic_hz(uint32_t hz);

#endif
