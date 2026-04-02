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

bool fs_stat(const char* path, FsNodeInfo* out)
{
    Fat16NodeInfo node;

    if (!fat16_stat(path, &node))
        return false;

    copy_node_info(out, &node);
    return true;
}

bool fs_list_dir(const char* path, FsListCallback callback, void* context)
{
    FsListAdapter adapter;

    if (callback == NULL)
        return false;

    adapter.callback = callback;
    adapter.context = context;
    return fat16_list_dir(path, fs_list_adapter, &adapter);
}

bool fs_read_file(const char* path, FsReadCallback callback, void* context)
{
    FsReadAdapter adapter;

    if (callback == NULL)
        return false;

    adapter.callback = callback;
    adapter.context = context;
    return fat16_read_file(path, fs_read_adapter, &adapter);
}
