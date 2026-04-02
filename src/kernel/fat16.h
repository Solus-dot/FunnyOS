#ifndef FUNNYOS_KERNEL_FAT16_H
#define FUNNYOS_KERNEL_FAT16_H

#include "../common/types.h"

typedef struct Fat16NodeInfo {
    char name[13];
    bool is_dir;
    uint32_t size;
} Fat16NodeInfo;

typedef enum Fat16Result {
    FAT16_OK,
    FAT16_NOT_FOUND,
    FAT16_ALREADY_EXISTS,
    FAT16_NOT_A_DIRECTORY,
    FAT16_IS_A_DIRECTORY,
    FAT16_DIRECTORY_NOT_EMPTY,
    FAT16_INVALID_NAME,
    FAT16_NO_SPACE,
    FAT16_IO_ERROR
} Fat16Result;

typedef bool (*Fat16ListCallback)(const Fat16NodeInfo* entry, void* context);
typedef bool (*Fat16ReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fat16_mount(uint32_t partition_lba_start, uint16_t bytes_per_sector);
Fat16Result fat16_stat(const char* path, Fat16NodeInfo* out);
Fat16Result fat16_list_dir(const char* path, Fat16ListCallback callback, void* context);
Fat16Result fat16_read_file(const char* path, Fat16ReadCallback callback, void* context);
Fat16Result fat16_write_file(const char* path, const uint8_t* data, uint32_t size, bool append);
Fat16Result fat16_make_dir(const char* path);
Fat16Result fat16_remove(const char* path);
Fat16Result fat16_rename(const char* old_path, const char* new_path);

#endif
