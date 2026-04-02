#include "block.h"
#include "ata.h"

static bool g_block_ready = false;

bool block_init(const BootInfo* boot_info)
{
    if (boot_info == NULL)
        return false;

    g_block_ready = ata_init(boot_info->boot_drive);
    return g_block_ready;
}

bool block_read_sectors(uint32_t lba, uint8_t count, void* out)
{
    if (!g_block_ready)
        return false;

    return ata_read_sectors(lba, count, out);
}

bool block_write_sectors(uint32_t lba, uint8_t count, const void* data)
{
    if (!g_block_ready)
        return false;

    return ata_write_sectors(lba, count, data);
}
