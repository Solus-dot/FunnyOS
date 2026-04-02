#include "fat16.h"
#include "ata.h"
#include "kstring.h"

#define FAT16_MAX_FAT_SECTORS 128u
#define FAT16_MAX_CLUSTER_SECTORS 8u
#define FAT16_SECTOR_SIZE 512u
#define FAT16_END_OF_CHAIN 0xFFF8u
#define FAT16_DIRECTORY 0x10u
#define FAT16_VOLUME_ID 0x08u
#define FAT16_LFN 0x0Fu

typedef struct __attribute__((packed)) BootSector {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t dir_entry_count;
    uint16_t total_sectors16;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
} BootSector;

typedef struct __attribute__((packed)) DirectoryEntry {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t created_time_tenths;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_date;
    uint16_t first_cluster_high;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t first_cluster_low;
    uint32_t size;
} DirectoryEntry;

typedef struct ResolvedPath {
    bool is_root;
    DirectoryEntry entry;
} ResolvedPath;

static uint16_t g_fat[FAT16_MAX_FAT_SECTORS * FAT16_SECTOR_SIZE / 2u];
static uint8_t g_sector_buffer[FAT16_SECTOR_SIZE];
static uint8_t g_cluster_buffer[FAT16_MAX_CLUSTER_SECTORS * FAT16_SECTOR_SIZE];

static bool g_mounted = false;
static uint16_t g_bytes_per_sector = 0;
static uint8_t g_sectors_per_cluster = 0;
static uint16_t g_root_entry_count = 0;
static uint32_t g_partition_lba = 0;
static uint32_t g_root_lba = 0;
static uint32_t g_root_sectors = 0;
static uint32_t g_data_lba = 0;
static uint32_t g_cluster_size = 0;

static bool read_sector(uint32_t lba, void* out)
{
    return ata_read_sectors(lba, 1, out);
}

static uint16_t fat16_next_cluster(uint16_t cluster)
{
    return g_fat[cluster];
}

static uint32_t cluster_to_lba(uint16_t cluster)
{
    return g_data_lba + (uint32_t)(cluster - 2u) * g_sectors_per_cluster;
}

static bool read_cluster(uint16_t cluster)
{
    if (g_sectors_per_cluster == 0 || g_sectors_per_cluster > FAT16_MAX_CLUSTER_SECTORS)
        return false;

    return ata_read_sectors(cluster_to_lba(cluster), g_sectors_per_cluster, g_cluster_buffer);
}

static bool should_skip_entry(const DirectoryEntry* entry)
{
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5)
        return true;
    if (entry->attributes == FAT16_LFN)
        return true;
    if ((entry->attributes & FAT16_VOLUME_ID) != 0)
        return true;
    return false;
}

static void format_name(const uint8_t raw[11], char out[13])
{
    uint32_t pos = 0;
    uint32_t i;
    bool has_ext = false;

    for (i = 8; i < 11; ++i) {
        if (raw[i] != ' ') {
            has_ext = true;
            break;
        }
    }

    for (i = 0; i < 8 && raw[i] != ' '; ++i)
        out[pos++] = (char)raw[i];

    if (has_ext) {
        out[pos++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; ++i)
            out[pos++] = (char)raw[i];
    }

    out[pos] = '\0';
}

static void make_fat_name(const char* name, uint8_t out[11])
{
    uint32_t i = 0;
    const char* dot = NULL;

    k_memset(out, ' ', 11);

    while (name[i] != '\0') {
        if (name[i] == '.') {
            dot = &name[i];
            break;
        }
        ++i;
    }

    if (dot == NULL)
        dot = name + i;

    for (i = 0; i < 8 && name[i] != '\0' && &name[i] < dot; ++i)
        out[i] = (uint8_t)k_toupper(name[i]);

    if (*dot == '.') {
        for (i = 0; i < 3 && dot[i + 1] != '\0'; ++i)
            out[8 + i] = (uint8_t)k_toupper(dot[i + 1]);
    }
}

static bool find_in_root(const uint8_t fat_name[11], DirectoryEntry* out)
{
    uint32_t sector;

    for (sector = 0; sector < g_root_sectors; ++sector) {
        uint32_t entry_index;
        DirectoryEntry* entries;

        if (!read_sector(g_root_lba + sector, g_sector_buffer))
            return false;

        entries = (DirectoryEntry*)g_sector_buffer;
        for (entry_index = 0; entry_index < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++entry_index) {
            DirectoryEntry* entry = &entries[entry_index];

            if (entry->name[0] == 0x00)
                return false;
            if (should_skip_entry(entry))
                continue;
            if (k_memcmp(entry->name, fat_name, 11) == 0) {
                *out = *entry;
                return true;
            }
        }
    }

    return false;
}

static bool find_in_cluster_dir(uint16_t first_cluster, const uint8_t fat_name[11], DirectoryEntry* out)
{
    uint16_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return false;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = g_cluster_size / sizeof(DirectoryEntry);
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];

            if (entry->name[0] == 0x00)
                return false;
            if (should_skip_entry(entry))
                continue;
            if (k_memcmp(entry->name, fat_name, 11) == 0) {
                *out = *entry;
                return true;
            }
        }

        cluster = fat16_next_cluster(cluster);
    }

    return false;
}

static bool resolve_path(const char* path, ResolvedPath* out)
{
    DirectoryEntry current = {0};
    bool have_current = false;
    const char* cursor = path;

    while (*cursor == '/')
        ++cursor;

    if (*cursor == '\0') {
        out->is_root = true;
        return true;
    }

    while (*cursor != '\0') {
        char component[16];
        const char* slash = cursor;
        uint32_t len = 0;
        uint8_t fat_name[11];
        bool found = false;

        while (*slash != '\0' && *slash != '/') {
            if (len + 1u >= sizeof(component))
                return false;
            component[len++] = *slash;
            ++slash;
        }
        component[len] = '\0';
        make_fat_name(component, fat_name);

        if (!have_current)
            found = find_in_root(fat_name, &current);
        else if ((current.attributes & FAT16_DIRECTORY) != 0)
            found = find_in_cluster_dir(current.first_cluster_low, fat_name, &current);
        else
            return false;

        if (!found)
            return false;

        have_current = true;

        while (*slash == '/')
            ++slash;
        if (*slash != '\0' && (current.attributes & FAT16_DIRECTORY) == 0)
            return false;

        cursor = slash;
    }

    out->is_root = false;
    out->entry = current;
    return true;
}

static FatDirEntry to_public_entry(const DirectoryEntry* entry)
{
    FatDirEntry out;

    format_name(entry->name, out.name);
    out.is_dir = (entry->attributes & FAT16_DIRECTORY) != 0;
    out.size = entry->size;
    return out;
}

static bool list_root_dir(Fat16ListCallback callback, void* context)
{
    uint32_t sector;

    for (sector = 0; sector < g_root_sectors; ++sector) {
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_sector(g_root_lba + sector, g_sector_buffer))
            return false;

        entries = (DirectoryEntry*)g_sector_buffer;
        for (i = 0; i < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++i) {
            DirectoryEntry* entry = &entries[i];
            FatDirEntry public_entry;

            if (entry->name[0] == 0x00)
                return true;
            if (should_skip_entry(entry))
                continue;

            public_entry = to_public_entry(entry);
            if (!callback(&public_entry, context))
                return true;
        }
    }

    return true;
}

static bool list_cluster_dir(uint16_t first_cluster, Fat16ListCallback callback, void* context)
{
    uint16_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t i;
        uint32_t entry_count;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return false;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = g_cluster_size / sizeof(DirectoryEntry);
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];
            FatDirEntry public_entry;

            if (entry->name[0] == 0x00)
                return true;
            if (should_skip_entry(entry))
                continue;
            if (entry->name[0] == '.')
                continue;

            public_entry = to_public_entry(entry);
            if (!callback(&public_entry, context))
                return true;
        }

        cluster = fat16_next_cluster(cluster);
    }

    return true;
}

bool fat16_mount(const BootInfo* boot_info)
{
    const BootSector* boot_sector;
    uint32_t fat_bytes;

    if (boot_info == NULL || boot_info->bytes_per_sector != FAT16_SECTOR_SIZE)
        return false;
    if (!read_sector(boot_info->partition_lba_start, g_sector_buffer))
        return false;
    boot_sector = (const BootSector*)g_sector_buffer;
    if (boot_sector->bytes_per_sector != FAT16_SECTOR_SIZE)
        return false;
    if (boot_sector->sectors_per_fat == 0 || boot_sector->sectors_per_fat > FAT16_MAX_FAT_SECTORS)
        return false;
    if (boot_sector->sectors_per_cluster == 0 || boot_sector->sectors_per_cluster > FAT16_MAX_CLUSTER_SECTORS)
        return false;

    g_partition_lba = boot_info->partition_lba_start;
    g_bytes_per_sector = boot_sector->bytes_per_sector;
    g_sectors_per_cluster = boot_sector->sectors_per_cluster;
    g_root_entry_count = boot_sector->dir_entry_count;
    g_root_sectors = ((uint32_t)g_root_entry_count * sizeof(DirectoryEntry) + (g_bytes_per_sector - 1u)) / g_bytes_per_sector;
    g_root_lba = g_partition_lba + boot_sector->reserved_sectors + (uint32_t)boot_sector->fat_count * boot_sector->sectors_per_fat;
    g_data_lba = g_root_lba + g_root_sectors;
    g_cluster_size = (uint32_t)g_sectors_per_cluster * g_bytes_per_sector;

    fat_bytes = (uint32_t)boot_sector->sectors_per_fat * g_bytes_per_sector;
    if (!ata_read_sectors(g_partition_lba + boot_sector->reserved_sectors, (uint8_t)boot_sector->sectors_per_fat, g_fat))
        return false;

    k_memset(g_sector_buffer, 0, sizeof(g_sector_buffer));
    k_memset(g_cluster_buffer, 0, sizeof(g_cluster_buffer));
    k_memset(((uint8_t*)g_fat) + fat_bytes, 0, sizeof(g_fat) - fat_bytes);
    g_mounted = true;
    return true;
}

bool fat16_stat(const char* path, FatDirEntry* out)
{
    ResolvedPath resolved;

    if (!g_mounted || path == NULL || out == NULL)
        return false;

    if (!resolve_path(path, &resolved))
        return false;

    if (resolved.is_root) {
        out->name[0] = '/';
        out->name[1] = '\0';
        out->is_dir = true;
        out->size = 0;
    } else {
        *out = to_public_entry(&resolved.entry);
    }

    return true;
}

bool fat16_list_dir(const char* path, Fat16ListCallback callback, void* context)
{
    ResolvedPath resolved;

    if (!g_mounted || path == NULL || callback == NULL)
        return false;
    if (!resolve_path(path, &resolved))
        return false;
    if (resolved.is_root)
        return list_root_dir(callback, context);
    if ((resolved.entry.attributes & FAT16_DIRECTORY) == 0)
        return false;

    return list_cluster_dir(resolved.entry.first_cluster_low, callback, context);
}

bool fat16_read_file(const char* path, Fat16ReadCallback callback, void* context)
{
    ResolvedPath resolved;
    uint32_t remaining;
    uint16_t cluster;

    if (!g_mounted || path == NULL || callback == NULL)
        return false;
    if (!resolve_path(path, &resolved) || resolved.is_root)
        return false;
    if ((resolved.entry.attributes & FAT16_DIRECTORY) != 0)
        return false;

    remaining = resolved.entry.size;
    cluster = resolved.entry.first_cluster_low;

    while (remaining > 0 && cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t chunk = g_cluster_size;

        if (!read_cluster(cluster))
            return false;
        if (chunk > remaining)
            chunk = remaining;
        if (!callback(g_cluster_buffer, chunk, context))
            return false;

        remaining -= chunk;
        cluster = fat16_next_cluster(cluster);
    }

    return remaining == 0;
}
