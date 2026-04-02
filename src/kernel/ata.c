#include "ata.h"
#include "io.h"

#define ATA_PRIMARY_IO_BASE 0x1F0u
#define ATA_PRIMARY_CTRL_BASE 0x3F6u
#define ATA_SECONDARY_IO_BASE 0x170u
#define ATA_SECONDARY_CTRL_BASE 0x376u

#define ATA_DATA_OFFSET 0x0u
#define ATA_SECTOR_COUNT_OFFSET 0x2u
#define ATA_LBA_LOW_OFFSET 0x3u
#define ATA_LBA_MID_OFFSET 0x4u
#define ATA_LBA_HIGH_OFFSET 0x5u
#define ATA_DRIVE_OFFSET 0x6u
#define ATA_STATUS_OFFSET 0x7u
#define ATA_COMMAND_OFFSET 0x7u

#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DF 0x20u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_BSY 0x80u

static uint16_t g_ata_io_base = ATA_PRIMARY_IO_BASE;
static uint16_t g_ata_ctrl_base = ATA_PRIMARY_CTRL_BASE;
static uint8_t g_ata_drive_head = 0xE0u;

bool ata_init(uint8_t bios_drive)
{
    if (bios_drive < 0x80u || bios_drive > 0x83u)
        return false;

    switch (bios_drive - 0x80u) {
    case 0u:
        g_ata_io_base = ATA_PRIMARY_IO_BASE;
        g_ata_ctrl_base = ATA_PRIMARY_CTRL_BASE;
        g_ata_drive_head = 0xE0u;
        break;
    case 1u:
        g_ata_io_base = ATA_PRIMARY_IO_BASE;
        g_ata_ctrl_base = ATA_PRIMARY_CTRL_BASE;
        g_ata_drive_head = 0xF0u;
        break;
    case 2u:
        g_ata_io_base = ATA_SECONDARY_IO_BASE;
        g_ata_ctrl_base = ATA_SECONDARY_CTRL_BASE;
        g_ata_drive_head = 0xE0u;
        break;
    case 3u:
        g_ata_io_base = ATA_SECONDARY_IO_BASE;
        g_ata_ctrl_base = ATA_SECONDARY_CTRL_BASE;
        g_ata_drive_head = 0xF0u;
        break;
    default:
        return false;
    }

    return true;
}

static void ata_io_wait(void)
{
    io_in8(g_ata_ctrl_base);
    io_in8(g_ata_ctrl_base);
    io_in8(g_ata_ctrl_base);
    io_in8(g_ata_ctrl_base);
}

static bool ata_wait_not_busy(void)
{
    uint32_t spins;

    for (spins = 0; spins < 100000u; ++spins) {
        if ((io_in8(g_ata_io_base + ATA_STATUS_OFFSET) & ATA_STATUS_BSY) == 0)
            return true;
        cpu_pause();
    }

    return false;
}

static bool ata_wait_drq(void)
{
    uint32_t spins;

    for (spins = 0; spins < 100000u; ++spins) {
        uint8_t status = io_in8(g_ata_io_base + ATA_STATUS_OFFSET);

        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0)
            return false;
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ) != 0)
            return true;

        cpu_pause();
    }

    return false;
}

bool ata_read_sectors(uint32_t lba, uint8_t count, void* out)
{
    uint8_t sector;
    uint16_t* words = (uint16_t*)out;

    if (count == 0)
        return true;
    if (lba > 0x0FFFFFFFu)
        return false;

    for (sector = 0; sector < count; ++sector) {
        uint32_t current_lba = lba + sector;
        uint16_t i;

        if (!ata_wait_not_busy())
            return false;

        io_out8(g_ata_io_base + ATA_DRIVE_OFFSET, (uint8_t)(g_ata_drive_head | ((current_lba >> 24) & 0x0Fu)));
        ata_io_wait();
        io_out8(g_ata_io_base + ATA_SECTOR_COUNT_OFFSET, 1);
        io_out8(g_ata_io_base + ATA_LBA_LOW_OFFSET, (uint8_t)(current_lba & 0xFFu));
        io_out8(g_ata_io_base + ATA_LBA_MID_OFFSET, (uint8_t)((current_lba >> 8) & 0xFFu));
        io_out8(g_ata_io_base + ATA_LBA_HIGH_OFFSET, (uint8_t)((current_lba >> 16) & 0xFFu));
        io_out8(g_ata_io_base + ATA_COMMAND_OFFSET, 0x20u);

        if (!ata_wait_drq())
            return false;

        for (i = 0; i < 256u; ++i)
            words[sector * 256u + i] = io_in16(g_ata_io_base + ATA_DATA_OFFSET);

        ata_io_wait();
    }

    return true;
}

bool ata_write_sectors(uint32_t lba, uint8_t count, const void* data)
{
    uint8_t sector;
    const uint16_t* words = (const uint16_t*)data;

    if (count == 0)
        return true;
    if (lba > 0x0FFFFFFFu)
        return false;

    for (sector = 0; sector < count; ++sector) {
        uint32_t current_lba = lba + sector;
        uint16_t i;

        if (!ata_wait_not_busy())
            return false;

        io_out8(g_ata_io_base + ATA_DRIVE_OFFSET, (uint8_t)(g_ata_drive_head | ((current_lba >> 24) & 0x0Fu)));
        ata_io_wait();
        io_out8(g_ata_io_base + ATA_SECTOR_COUNT_OFFSET, 1);
        io_out8(g_ata_io_base + ATA_LBA_LOW_OFFSET, (uint8_t)(current_lba & 0xFFu));
        io_out8(g_ata_io_base + ATA_LBA_MID_OFFSET, (uint8_t)((current_lba >> 8) & 0xFFu));
        io_out8(g_ata_io_base + ATA_LBA_HIGH_OFFSET, (uint8_t)((current_lba >> 16) & 0xFFu));
        io_out8(g_ata_io_base + ATA_COMMAND_OFFSET, 0x30u);

        if (!ata_wait_drq())
            return false;

        for (i = 0; i < 256u; ++i)
            io_out16(g_ata_io_base + ATA_DATA_OFFSET, words[sector * 256u + i]);

        ata_io_wait();
        if (!ata_wait_not_busy())
            return false;
    }

    return true;
}
