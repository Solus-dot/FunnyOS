#ifndef FUNNYOS_KERNEL_FS_H
#define FUNNYOS_KERNEL_FS_H

#include "../common/bootinfo.h"
#include "../common/types.h"

typedef enum FsNodeType {
    FS_NODE_FILE,
    FS_NODE_DIR
} FsNodeType;

typedef struct FsNodeInfo {
    char name[13];
    FsNodeType type;
    uint32_t size;
} FsNodeInfo;

typedef bool (*FsListCallback)(const FsNodeInfo* entry, void* context);
typedef bool (*FsReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool fs_init(const BootInfo* boot_info);
bool fs_stat(const char* path, FsNodeInfo* out);
bool fs_list_dir(const char* path, FsListCallback callback, void* context);
bool fs_read_file(const char* path, FsReadCallback callback, void* context);

#endif
