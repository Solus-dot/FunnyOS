#ifndef FUNNYOS_BOOTINFO_H
#define FUNNYOS_BOOTINFO_H

#include "types.h"

#define BOOTINFO_MAGIC 0x534f4e46u

typedef struct BootInfo {
    uint32_t magic;
    uint16_t boot_drive;
    uint16_t boot_device_type;
    uint32_t memory_map_addr;
    uint32_t memory_map_entries;
    uint32_t root_dir_snapshot_addr;
    uint32_t root_dir_snapshot_count;
    uint32_t demo_file_addr;
    uint32_t demo_file_size;
    uint16_t screen_columns;
    uint16_t screen_rows;
} BootInfo;

#endif
