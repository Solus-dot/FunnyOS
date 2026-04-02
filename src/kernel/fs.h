#ifndef FUNNYOS_KERNEL_FS_H
#define FUNNYOS_KERNEL_FS_H

#include "../common/bootinfo.h"
#include "../common/types.h"

typedef enum FsNodeType {
    FS_NODE_FILE,
    FS_NODE_DIR
} FsNodeType;

typedef enum FsResult {
    FS_OK,
    FS_NOT_FOUND,
    FS_ALREADY_EXISTS,
    FS_NOT_A_DIRECTORY,
    FS_IS_A_DIRECTORY,
    FS_DIRECTORY_NOT_EMPTY,
    FS_INVALID_NAME,
    FS_NO_SPACE,
    FS_IO_ERROR
} FsResult;

typedef struct FsNodeInfo {
    char name[13];
    FsNodeType type;
    uint32_t size;
} FsNodeInfo;

typedef bool (*FsListCallback)(const FsNodeInfo* entry, void* context);
typedef bool (*FsReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fs_init(const BootInfo* boot_info);
FsResult fs_stat(const char* path, FsNodeInfo* out);
FsResult fs_list_dir(const char* path, FsListCallback callback, void* context);
FsResult fs_read_file(const char* path, FsReadCallback callback, void* context);
FsResult fs_write_file(const char* path, const uint8_t* data, uint32_t size, bool append);
FsResult fs_make_dir(const char* path);
FsResult fs_remove(const char* path);
FsResult fs_rename(const char* old_path, const char* new_path);

#endif
