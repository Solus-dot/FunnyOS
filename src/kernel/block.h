#ifndef FUNNYOS_KERNEL_BLOCK_H
#define FUNNYOS_KERNEL_BLOCK_H

#include "../common/bootinfo.h"
#include "../common/types.h"

bool block_init(const BootInfo* boot_info);
bool block_read_sectors(uint32_t lba, uint8_t count, void* out);

#endif
