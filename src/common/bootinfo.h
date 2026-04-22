#ifndef FUNNYOS_BOOTINFO_H
#define FUNNYOS_BOOTINFO_H

#include "types.h"

#define BOOTINFO_MAGIC 0x534f4e46u

#define BOOTINFO_CONSOLE_TEXT 0x0001u
#define BOOTINFO_CONSOLE_VGA_TEXT 0x0002u
#define BOOTINFO_CONSOLE_FRAMEBUFFER 0x0004u

#define BOOTINFO_FRAMEBUFFER_FORMAT_BGRX 1u
#define BOOTINFO_FRAMEBUFFER_FORMAT_RGBX 2u
#define BOOTINFO_DEVICE_PATH_CAPACITY 128u

typedef struct BootMemoryMap {
    uintptr_t base;
    size_t size;
    uint32_t descriptor_size;
    uint32_t descriptor_version;
} BootMemoryMap;

typedef struct BootInfo {
    uint32_t magic;
    uint16_t bytes_per_sector;
    uint32_t partition_lba_start;
    uint32_t partition_sector_count;
    uintptr_t acpi_rsdp;
    uint8_t acpi_revision;
    uint8_t reserved0[7];
    uint16_t boot_device_path_size;
    uint8_t boot_device_path[BOOTINFO_DEVICE_PATH_CAPACITY];
    uint16_t console_flags;
    uint16_t screen_columns;
    uint16_t screen_rows;
    uintptr_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pixels_per_scanline;
    uint32_t framebuffer_format;
    BootMemoryMap memory_map;
} BootInfo;

#endif
