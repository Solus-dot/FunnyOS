#include "fs.h"
#include "block.h"
#include "fat16.h"

typedef struct FsListAdapter {
    FsListCallback callback;
    void* context;
} FsListAdapter;

typedef struct FsReadAdapter {
    FsReadCallback callback;
    void* context;
} FsReadAdapter;

static FsResult map_result(Fat16Result result)
{
    switch (result) {
    case FAT16_OK:
        return FS_OK;
    case FAT16_NOT_FOUND:
        return FS_NOT_FOUND;
    case FAT16_ALREADY_EXISTS:
        return FS_ALREADY_EXISTS;
    case FAT16_NOT_A_DIRECTORY:
        return FS_NOT_A_DIRECTORY;
    case FAT16_IS_A_DIRECTORY:
        return FS_IS_A_DIRECTORY;
    case FAT16_DIRECTORY_NOT_EMPTY:
        return FS_DIRECTORY_NOT_EMPTY;
    case FAT16_INVALID_NAME:
        return FS_INVALID_NAME;
    case FAT16_NO_SPACE:
        return FS_NO_SPACE;
    case FAT16_IO_ERROR:
    default:
        return FS_IO_ERROR;
    }
}

static void copy_node_info(FsNodeInfo* out, const Fat16NodeInfo* in)
{
    uint32_t i = 0;

    while (i < sizeof(out->name) - 1u && in->name[i] != '\0') {
        out->name[i] = in->name[i];
        ++i;
    }
    out->name[i] = '\0';
    out->type = in->is_dir ? FS_NODE_DIR : FS_NODE_FILE;
    out->size = in->size;
}

static bool fs_list_adapter(const Fat16NodeInfo* entry, void* context)
{
    FsListAdapter* adapter = (FsListAdapter*)context;
    FsNodeInfo node;

    copy_node_info(&node, entry);
    return adapter->callback(&node, adapter->context);
}

static bool fs_read_adapter(const uint8_t* data, uint32_t length, void* context)
{
    FsReadAdapter* adapter = (FsReadAdapter*)context;
    return adapter->callback(data, length, adapter->context);
}

bool fs_init(const BootInfo* boot_info)
{
    if (!block_init(boot_info))
        return false;

    return fat16_mount(boot_info->partition_lba_start, boot_info->bytes_per_sector);
}

FsResult fs_stat(const char* path, FsNodeInfo* out)
{
    Fat16NodeInfo node;
    Fat16Result result;

    if (out == NULL)
        return FS_IO_ERROR;
    result = fat16_stat(path, &node);
    if (result != FAT16_OK)
        return map_result(result);

    copy_node_info(out, &node);
    return FS_OK;
}

FsResult fs_list_dir(const char* path, FsListCallback callback, void* context)
{
    FsListAdapter adapter;

    if (callback == NULL)
        return FS_IO_ERROR;

    adapter.callback = callback;
    adapter.context = context;
    return map_result(fat16_list_dir(path, fs_list_adapter, &adapter));
}

FsResult fs_read_file(const char* path, FsReadCallback callback, void* context)
{
    FsReadAdapter adapter;

    if (callback == NULL)
        return FS_IO_ERROR;

    adapter.callback = callback;
    adapter.context = context;
    return map_result(fat16_read_file(path, fs_read_adapter, &adapter));
}

FsResult fs_write_file(const char* path, const uint8_t* data, uint32_t size, bool append)
{
    return map_result(fat16_write_file(path, data, size, append));
}

FsResult fs_make_dir(const char* path)
{
    return map_result(fat16_make_dir(path));
}

FsResult fs_remove(const char* path)
{
    return map_result(fat16_remove(path));
}

FsResult fs_rename(const char* old_path, const char* new_path)
{
    return map_result(fat16_rename(old_path, new_path));
}
