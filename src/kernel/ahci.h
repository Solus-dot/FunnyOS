#ifndef FUNNYOS_KERNEL_AHCI_H
#define FUNNYOS_KERNEL_AHCI_H

#include "../common/types.h"
#include "pci.h"

typedef struct AhciDeviceAddress {
    PciAddress controller;
    uint8_t port;
} AhciDeviceAddress;

bool ahci_init(const AhciDeviceAddress* device);
bool ahci_read_sectors(uint32_t lba, uint8_t count, void* out);
bool ahci_write_sectors(uint32_t lba, uint8_t count, const void* data);

#endif
