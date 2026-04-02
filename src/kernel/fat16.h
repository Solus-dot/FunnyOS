#ifndef FUNNYOS_KERNEL_FAT16_H
#define FUNNYOS_KERNEL_FAT16_H

#include "../common/types.h"

typedef struct Fat16NodeInfo {
    char name[13];
    bool is_dir;
    uint32_t size;
} Fat16NodeInfo;

typedef bool (*Fat16ListCallback)(const Fat16NodeInfo* entry, void* context);
typedef bool (*Fat16ReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fat16_mount(uint32_t partition_lba_start, uint16_t bytes_per_sector);
bool fat16_stat(const char* path, Fat16NodeInfo* out);
bool fat16_list_dir(const char* path, Fat16ListCallback callback, void* context);
bool fat16_read_file(const char* path, Fat16ReadCallback callback, void* context);

#endif
