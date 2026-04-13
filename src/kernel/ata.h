#ifndef FUNNYOS_KERNEL_ATA_H
#define FUNNYOS_KERNEL_ATA_H

#include "../common/types.h"

typedef struct AtaDeviceAddress {
    uint8_t channel;
    uint8_t drive;
} AtaDeviceAddress;

bool ata_init(const AtaDeviceAddress* device);
bool ata_read_sectors(uint32_t lba, uint8_t count, void* out);
bool ata_write_sectors(uint32_t lba, uint8_t count, const void* data);

#endif
