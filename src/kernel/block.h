#ifndef FUNNYOS_KERNEL_BLOCK_H
#define FUNNYOS_KERNEL_BLOCK_H

#include "../common/bootinfo.h"
#include "../common/types.h"

typedef enum BlockBackendKind {
    BLOCK_BACKEND_NONE = 0u,
    BLOCK_BACKEND_ATA_PIO = 1u,
    BLOCK_BACKEND_AHCI = 2u
} BlockBackendKind;

bool block_init(const BootInfo* boot_info);
bool block_read_sectors(uint32_t lba, uint8_t count, void* out);
bool block_write_sectors(uint32_t lba, uint8_t count, const void* data);
BlockBackendKind block_backend_kind(void);

#endif
