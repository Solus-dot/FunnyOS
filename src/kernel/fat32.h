#ifndef FUNNYOS_KERNEL_FAT32_H
#define FUNNYOS_KERNEL_FAT32_H

#include "../common/types.h"

#define FAT32_NAME_CAPACITY 128u

typedef struct Fat32NodeInfo {
    char name[FAT32_NAME_CAPACITY];
    bool is_dir;
    uint32_t size;
} Fat32NodeInfo;

typedef enum Fat32Result {
    FAT32_OK,
    FAT32_NOT_FOUND,
    FAT32_ALREADY_EXISTS,
    FAT32_NOT_A_DIRECTORY,
    FAT32_IS_A_DIRECTORY,
    FAT32_DIRECTORY_NOT_EMPTY,
    FAT32_INVALID_NAME,
    FAT32_NO_SPACE,
    FAT32_IO_ERROR
} Fat32Result;

typedef bool (*Fat32ListCallback)(const Fat32NodeInfo* entry, void* context);
typedef bool (*Fat32ReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fat32_mount(uint32_t partition_lba_start, uint16_t bytes_per_sector);
Fat32Result fat32_stat(const char* path, Fat32NodeInfo* out);
Fat32Result fat32_list_dir(const char* path, Fat32ListCallback callback, void* context);
Fat32Result fat32_read_file(const char* path, Fat32ReadCallback callback, void* context);
Fat32Result fat32_write_file(const char* path, const uint8_t* data, uint32_t size, bool append);
Fat32Result fat32_make_dir(const char* path);
Fat32Result fat32_remove(const char* path);
Fat32Result fat32_rename(const char* old_path, const char* new_path);

#endif
