#ifndef FUNNYOS_KERNEL_ATA_H
#define FUNNYOS_KERNEL_ATA_H

#include "../common/types.h"

bool ata_init(uint8_t bios_drive);
bool ata_read_sectors(uint32_t lba, uint8_t count, void* out);

#endif
