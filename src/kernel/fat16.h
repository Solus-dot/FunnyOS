#ifndef FUNNYOS_KERNEL_FAT16_H
#define FUNNYOS_KERNEL_FAT16_H

#include "../common/bootinfo.h"
#include "../common/types.h"

typedef struct FatDirEntry {
    char name[13];
    bool is_dir;
    uint32_t size;
} FatDirEntry;

typedef bool (*Fat16ListCallback)(const FatDirEntry* entry, void* context);
typedef bool (*Fat16ReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fat16_mount(const BootInfo* boot_info);
bool fat16_stat(const char* path, FatDirEntry* out);
bool fat16_list_dir(const char* path, Fat16ListCallback callback, void* context);
bool fat16_read_file(const char* path, Fat16ReadCallback callback, void* context);

#endif
