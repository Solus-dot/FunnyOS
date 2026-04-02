#ifndef FUNNYOS_BOOTINFO_H
#define FUNNYOS_BOOTINFO_H

#include "types.h"

#define BOOTINFO_MAGIC 0x534f4e46u

typedef struct BootInfo {
    uint32_t magic;
    uint8_t boot_drive;
    uint8_t boot_partition_index;
    uint16_t bytes_per_sector;
    uint32_t partition_lba_start;
    uint32_t partition_sector_count;
    uint16_t screen_columns;
    uint16_t screen_rows;
} BootInfo;

#endif
