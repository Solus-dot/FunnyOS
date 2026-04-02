#include "fat16.h"
#include "block.h"
#include "kstring.h"

#define FAT16_MAX_FAT_SECTORS 128u
#define FAT16_MAX_CLUSTER_SECTORS 8u
#define FAT16_SECTOR_SIZE 512u
#define FAT16_END_OF_CHAIN 0xFFF8u
#define FAT16_DIRECTORY 0x10u
#define FAT16_VOLUME_ID 0x08u
#define FAT16_LFN 0x0Fu
#define FAT16_DELETED 0xE5u

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

typedef struct DirectoryRef {
    bool is_root;
    uint16_t first_cluster;
} DirectoryRef;

typedef struct EntryRef {
    bool found;
    uint32_t sector_lba;
    uint32_t index_in_sector;
    DirectoryEntry entry;
} EntryRef;

static uint16_t g_fat[FAT16_MAX_FAT_SECTORS * FAT16_SECTOR_SIZE / 2u];
static uint8_t g_sector_buffer[FAT16_SECTOR_SIZE];
static uint8_t g_cluster_buffer[FAT16_MAX_CLUSTER_SECTORS * FAT16_SECTOR_SIZE];

static bool g_mounted = false;
static uint16_t g_bytes_per_sector = 0;
static uint8_t g_sectors_per_cluster = 0;
static uint16_t g_root_entry_count = 0;
static uint8_t g_fat_count = 0;
static uint16_t g_reserved_sectors = 0;
static uint16_t g_sectors_per_fat = 0;
static uint32_t g_partition_lba = 0;
static uint32_t g_root_lba = 0;
static uint32_t g_root_sectors = 0;
static uint32_t g_data_lba = 0;
static uint32_t g_cluster_size = 0;
static uint32_t g_total_clusters = 0;
static uint32_t g_fat_lba = 0;

static bool read_sector(uint32_t lba, void* out)
{
    return block_read_sectors(lba, 1, out);
}

static bool write_sector(uint32_t lba, const void* data)
{
    return block_write_sectors(lba, 1, data);
}

static uint16_t fat16_next_cluster(uint16_t cluster)
{
    return g_fat[cluster];
}

static void fat16_set_cluster(uint16_t cluster, uint16_t value)
{
    g_fat[cluster] = value;
}

static uint32_t cluster_to_lba(uint16_t cluster)
{
    return g_data_lba + (uint32_t)(cluster - 2u) * g_sectors_per_cluster;
}

static bool read_cluster(uint16_t cluster)
{
    if (g_sectors_per_cluster == 0 || g_sectors_per_cluster > FAT16_MAX_CLUSTER_SECTORS)
        return false;

    return block_read_sectors(cluster_to_lba(cluster), g_sectors_per_cluster, g_cluster_buffer);
}

static bool write_cluster(uint16_t cluster)
{
    if (g_sectors_per_cluster == 0 || g_sectors_per_cluster > FAT16_MAX_CLUSTER_SECTORS)
        return false;

    return block_write_sectors(cluster_to_lba(cluster), g_sectors_per_cluster, g_cluster_buffer);
}

static bool flush_fat(void)
{
    uint32_t i;

    for (i = 0; i < g_fat_count; ++i) {
        if (!block_write_sectors(g_fat_lba + (uint32_t)i * g_sectors_per_fat, (uint8_t)g_sectors_per_fat, g_fat))
            return false;
    }

    return true;
}

static bool should_skip_entry(const DirectoryEntry* entry)
{
    if (entry->name[0] == 0x00 || entry->name[0] == FAT16_DELETED)
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

static bool make_fat_name(const char* name, uint8_t out[11])
{
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    bool seen_dot = false;

    if (name == NULL || *name == '\0')
        return false;
    if ((name[0] == '.' && name[1] == '\0') || (name[0] == '.' && name[1] == '.' && name[2] == '\0'))
        return false;

    k_memset(out, ' ', 11);

    while (*name != '\0') {
        char c = *name++;

        if (c == '.') {
            if (seen_dot)
                return false;
            seen_dot = true;
            continue;
        }
        if (c == '/' || k_is_space(c))
            return false;

        if (!seen_dot) {
            if (base_len >= 8u)
                return false;
            out[base_len++] = (uint8_t)k_toupper(c);
        } else {
            if (ext_len >= 3u)
                return false;
            out[8u + ext_len++] = (uint8_t)k_toupper(c);
        }
    }

    return base_len != 0u;
}

static Fat16NodeInfo decode_node_info(const DirectoryEntry* entry)
{
    Fat16NodeInfo out;

    format_name(entry->name, out.name);
    out.is_dir = (entry->attributes & FAT16_DIRECTORY) != 0;
    out.size = entry->size;
    return out;
}

static Fat16Result split_path(const char* path, char* parent_out, char* name_out)
{
    const char* cursor;
    const char* last_slash = NULL;
    uint32_t parent_len = 0;
    uint32_t name_len = 0;

    if (path == NULL || parent_out == NULL || name_out == NULL)
        return FAT16_INVALID_NAME;
    if (path[0] != '/' || path[1] == '\0')
        return FAT16_INVALID_NAME;

    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/')
            last_slash = cursor;
    }

    if (last_slash == NULL || last_slash[1] == '\0')
        return FAT16_INVALID_NAME;

    parent_len = (uint32_t)(last_slash - path);
    if (parent_len == 0u) {
        parent_out[0] = '/';
        parent_out[1] = '\0';
    } else {
        uint32_t i;

        for (i = 0; i < parent_len; ++i)
            parent_out[i] = path[i];
        parent_out[parent_len] = '\0';
    }

    cursor = last_slash + 1;
    while (cursor[name_len] != '\0') {
        if (name_len + 1u >= 13u)
            return FAT16_INVALID_NAME;
        name_out[name_len] = cursor[name_len];
        ++name_len;
    }
    name_out[name_len] = '\0';
    return FAT16_OK;
}

static Fat16Result resolve_path(const char* path, ResolvedPath* out)
{
    DirectoryEntry current;
    bool have_current = false;
    const char* cursor = path;

    while (*cursor == '/')
        ++cursor;

    if (*cursor == '\0') {
        out->is_root = true;
        return FAT16_OK;
    }

    k_memset(&current, 0, sizeof(current));
    while (*cursor != '\0') {
        char component[16];
        const char* slash = cursor;
        uint32_t len = 0;
        uint8_t fat_name[11];
        DirectoryRef directory;
        Fat16Result result;

        while (*slash != '\0' && *slash != '/') {
            if (len + 1u >= sizeof(component))
                return FAT16_INVALID_NAME;
            component[len++] = *slash;
            ++slash;
        }
        component[len] = '\0';
        if (!make_fat_name(component, fat_name))
            return FAT16_INVALID_NAME;

        directory.is_root = !have_current;
        directory.first_cluster = have_current ? current.first_cluster_low : 0;

        result = FAT16_NOT_FOUND;
        if (directory.is_root) {
            uint32_t sector;

            for (sector = 0; sector < g_root_sectors; ++sector) {
                uint32_t i;
                DirectoryEntry* entries;

                if (!read_sector(g_root_lba + sector, g_sector_buffer))
                    return FAT16_IO_ERROR;

                entries = (DirectoryEntry*)g_sector_buffer;
                for (i = 0; i < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++i) {
                    DirectoryEntry* entry = &entries[i];

                    if (entry->name[0] == 0x00)
                        goto root_done;
                    if (should_skip_entry(entry))
                        continue;
                    if (k_memcmp(entry->name, fat_name, 11) == 0) {
                        current = *entry;
                        have_current = true;
                        result = FAT16_OK;
                        goto root_done;
                    }
                }
            }
root_done:
            if (result != FAT16_OK)
                return result;
        } else {
            uint16_t cluster = current.first_cluster_low;

            if ((current.attributes & FAT16_DIRECTORY) == 0)
                return FAT16_NOT_A_DIRECTORY;

            result = FAT16_NOT_FOUND;
            while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
                uint32_t entry_count;
                uint32_t i;
                DirectoryEntry* entries;

                if (!read_cluster(cluster))
                    return FAT16_IO_ERROR;

                entries = (DirectoryEntry*)g_cluster_buffer;
                entry_count = g_cluster_size / sizeof(DirectoryEntry);
                for (i = 0; i < entry_count; ++i) {
                    DirectoryEntry* entry = &entries[i];

                    if (entry->name[0] == 0x00)
                        goto cluster_done;
                    if (should_skip_entry(entry))
                        continue;
                    if (k_memcmp(entry->name, fat_name, 11) == 0) {
                        current = *entry;
                        have_current = true;
                        result = FAT16_OK;
                        goto cluster_done;
                    }
                }

                cluster = fat16_next_cluster(cluster);
            }
cluster_done:
            if (result != FAT16_OK)
                return result;
        }

        while (*slash == '/')
            ++slash;
        if (*slash != '\0' && (current.attributes & FAT16_DIRECTORY) == 0)
            return FAT16_NOT_A_DIRECTORY;

        cursor = slash;
    }

    out->is_root = false;
    out->entry = current;
    return FAT16_OK;
}

static Fat16Result resolve_directory(const char* path, DirectoryRef* out)
{
    ResolvedPath resolved;
    Fat16Result result;

    result = resolve_path(path, &resolved);
    if (result != FAT16_OK)
        return result;

    if (resolved.is_root) {
        out->is_root = true;
        out->first_cluster = 0;
        return FAT16_OK;
    }
    if ((resolved.entry.attributes & FAT16_DIRECTORY) == 0)
        return FAT16_NOT_A_DIRECTORY;

    out->is_root = false;
    out->first_cluster = resolved.entry.first_cluster_low;
    return FAT16_OK;
}

static Fat16Result find_entry_in_root(const uint8_t fat_name[11], EntryRef* out)
{
    uint32_t sector;

    out->found = false;
    for (sector = 0; sector < g_root_sectors; ++sector) {
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_sector(g_root_lba + sector, g_sector_buffer))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_sector_buffer;
        for (i = 0; i < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++i) {
            DirectoryEntry* entry = &entries[i];

            if (entry->name[0] == 0x00)
                return FAT16_NOT_FOUND;
            if (should_skip_entry(entry))
                continue;
            if (k_memcmp(entry->name, fat_name, 11) == 0) {
                out->found = true;
                out->sector_lba = g_root_lba + sector;
                out->index_in_sector = i;
                out->entry = *entry;
                return FAT16_OK;
            }
        }
    }

    return FAT16_NOT_FOUND;
}

static Fat16Result find_entry_in_cluster_dir(uint16_t first_cluster, const uint8_t fat_name[11], EntryRef* out)
{
    uint16_t cluster = first_cluster;

    out->found = false;
    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = g_cluster_size / sizeof(DirectoryEntry);
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];

            if (entry->name[0] == 0x00)
                return FAT16_NOT_FOUND;
            if (should_skip_entry(entry))
                continue;
            if (k_memcmp(entry->name, fat_name, 11) == 0) {
                out->found = true;
                out->sector_lba = cluster_to_lba(cluster) + (i / (FAT16_SECTOR_SIZE / sizeof(DirectoryEntry)));
                out->index_in_sector = i % (FAT16_SECTOR_SIZE / sizeof(DirectoryEntry));
                out->entry = *entry;
                return FAT16_OK;
            }
        }

        cluster = fat16_next_cluster(cluster);
    }

    return FAT16_NOT_FOUND;
}

static Fat16Result find_entry_in_directory(const DirectoryRef* directory, const uint8_t fat_name[11], EntryRef* out)
{
    if (directory->is_root)
        return find_entry_in_root(fat_name, out);
    return find_entry_in_cluster_dir(directory->first_cluster, fat_name, out);
}

static Fat16Result find_free_entry_in_root(EntryRef* out)
{
    uint32_t sector;

    for (sector = 0; sector < g_root_sectors; ++sector) {
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_sector(g_root_lba + sector, g_sector_buffer))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_sector_buffer;
        for (i = 0; i < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++i) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == FAT16_DELETED) {
                out->found = false;
                out->sector_lba = g_root_lba + sector;
                out->index_in_sector = i;
                return FAT16_OK;
            }
        }
    }

    return FAT16_NO_SPACE;
}

static uint16_t find_free_cluster(void)
{
    uint16_t cluster;

    for (cluster = 2u; cluster < (uint16_t)g_total_clusters; ++cluster) {
        if (g_fat[cluster] == 0u)
            return cluster;
    }

    return 0u;
}

static void free_cluster_chain_in_memory(uint16_t first_cluster)
{
    uint16_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint16_t next = fat16_next_cluster(cluster);
        fat16_set_cluster(cluster, 0u);
        cluster = next;
    }
}

static Fat16Result trim_cluster_chain(uint16_t* first_cluster, uint32_t required_count)
{
    uint16_t cluster;
    uint16_t next;

    if (first_cluster == NULL)
        return FAT16_IO_ERROR;

    if (required_count == 0u) {
        if (*first_cluster >= 2u)
            free_cluster_chain_in_memory(*first_cluster);
        *first_cluster = 0u;
        return FAT16_OK;
    }
    if (*first_cluster < 2u)
        return FAT16_OK;

    cluster = *first_cluster;
    while (required_count > 1u && cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        cluster = fat16_next_cluster(cluster);
        --required_count;
    }

    if (cluster < 2u || cluster >= FAT16_END_OF_CHAIN)
        return FAT16_IO_ERROR;

    next = fat16_next_cluster(cluster);
    fat16_set_cluster(cluster, FAT16_END_OF_CHAIN);
    if (next >= 2u && next < FAT16_END_OF_CHAIN)
        free_cluster_chain_in_memory(next);

    return FAT16_OK;
}

static Fat16Result append_cluster_to_chain(uint16_t first_cluster, uint16_t* new_cluster_out)
{
    uint16_t cluster;
    uint16_t next;
    uint16_t free_cluster = find_free_cluster();

    if (free_cluster == 0u)
        return FAT16_NO_SPACE;

    fat16_set_cluster(free_cluster, FAT16_END_OF_CHAIN);
    if (first_cluster == 0u) {
        *new_cluster_out = free_cluster;
        return FAT16_OK;
    }

    cluster = first_cluster;
    next = fat16_next_cluster(cluster);
    while (next >= 2u && next < FAT16_END_OF_CHAIN) {
        cluster = next;
        next = fat16_next_cluster(cluster);
    }

    fat16_set_cluster(cluster, free_cluster);
    *new_cluster_out = free_cluster;
    return FAT16_OK;
}

static Fat16Result zero_cluster(uint16_t cluster)
{
    k_memset(g_cluster_buffer, 0, g_cluster_size);
    return write_cluster(cluster) ? FAT16_OK : FAT16_IO_ERROR;
}

static Fat16Result allocate_cluster_chain(uint32_t count, uint16_t* first_cluster_out)
{
    uint16_t first_cluster = 0u;
    uint16_t last_cluster = 0u;
    uint32_t i;

    if (count == 0u) {
        *first_cluster_out = 0u;
        return FAT16_OK;
    }

    for (i = 0; i < count; ++i) {
        uint16_t cluster = find_free_cluster();

        if (cluster == 0u) {
            free_cluster_chain_in_memory(first_cluster);
            return FAT16_NO_SPACE;
        }

        fat16_set_cluster(cluster, FAT16_END_OF_CHAIN);
        if (last_cluster != 0u)
            fat16_set_cluster(last_cluster, cluster);
        else
            first_cluster = cluster;
        last_cluster = cluster;
    }

    for (i = 0, last_cluster = first_cluster; i < count && last_cluster >= 2u && last_cluster < FAT16_END_OF_CHAIN; ++i) {
        Fat16Result result = zero_cluster(last_cluster);
        uint16_t next = fat16_next_cluster(last_cluster);

        if (result != FAT16_OK) {
            free_cluster_chain_in_memory(first_cluster);
            return result;
        }
        if (next >= FAT16_END_OF_CHAIN)
            break;
        last_cluster = next;
    }

    *first_cluster_out = first_cluster;
    return FAT16_OK;
}

static uint32_t clusters_for_size(uint32_t size)
{
    if (size == 0u)
        return 0u;
    return (size + g_cluster_size - 1u) / g_cluster_size;
}

static uint16_t get_cluster_at_index(uint16_t first_cluster, uint32_t index)
{
    uint16_t cluster = first_cluster;

    while (index != 0u && cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        cluster = fat16_next_cluster(cluster);
        --index;
    }

    return cluster;
}

static Fat16Result ensure_cluster_chain_length(uint16_t* first_cluster, uint32_t current_count, uint32_t required_count)
{
    Fat16Result result;
    uint32_t i;

    if (required_count <= current_count)
        return FAT16_OK;

    if (current_count == 0u) {
        result = allocate_cluster_chain(required_count, first_cluster);
        return result;
    }

    for (i = current_count; i < required_count; ++i) {
        uint16_t new_cluster;

        result = append_cluster_to_chain(*first_cluster, &new_cluster);
        if (result != FAT16_OK)
            return result;
        result = zero_cluster(new_cluster);
        if (result != FAT16_OK) {
            fat16_set_cluster(new_cluster, 0u);
            return result;
        }
    }

    return FAT16_OK;
}

static Fat16Result write_entry_at(const EntryRef* ref, const DirectoryEntry* entry)
{
    DirectoryEntry* entries;

    if (!read_sector(ref->sector_lba, g_sector_buffer))
        return FAT16_IO_ERROR;

    entries = (DirectoryEntry*)g_sector_buffer;
    entries[ref->index_in_sector] = *entry;
    if (!write_sector(ref->sector_lba, g_sector_buffer))
        return FAT16_IO_ERROR;

    return FAT16_OK;
}

static Fat16Result mark_entry_deleted(const EntryRef* ref)
{
    DirectoryEntry* entries;

    if (!read_sector(ref->sector_lba, g_sector_buffer))
        return FAT16_IO_ERROR;

    entries = (DirectoryEntry*)g_sector_buffer;
    entries[ref->index_in_sector].name[0] = FAT16_DELETED;
    if (!write_sector(ref->sector_lba, g_sector_buffer))
        return FAT16_IO_ERROR;

    return FAT16_OK;
}

static Fat16Result find_free_entry_in_cluster_dir(uint16_t first_cluster, EntryRef* out)
{
    uint16_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = g_cluster_size / sizeof(DirectoryEntry);
        for (i = 0; i < entry_count; ++i) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == FAT16_DELETED) {
                out->found = false;
                out->sector_lba = cluster_to_lba(cluster) + (i / (FAT16_SECTOR_SIZE / sizeof(DirectoryEntry)));
                out->index_in_sector = i % (FAT16_SECTOR_SIZE / sizeof(DirectoryEntry));
                return FAT16_OK;
            }
        }

        if (fat16_next_cluster(cluster) >= FAT16_END_OF_CHAIN)
            break;
        cluster = fat16_next_cluster(cluster);
    }

    {
        uint16_t new_cluster = 0u;
        Fat16Result result = append_cluster_to_chain(first_cluster, &new_cluster);

        if (result != FAT16_OK)
            return result;
        result = zero_cluster(new_cluster);
        if (result != FAT16_OK)
            return result;
        if (!flush_fat())
            return FAT16_IO_ERROR;

        out->found = false;
        out->sector_lba = cluster_to_lba(new_cluster);
        out->index_in_sector = 0u;
        return FAT16_OK;
    }
}

static Fat16Result find_free_entry_in_directory(const DirectoryRef* directory, EntryRef* out)
{
    if (directory->is_root)
        return find_free_entry_in_root(out);
    return find_free_entry_in_cluster_dir(directory->first_cluster, out);
}

static bool directory_entry_is_special(const DirectoryEntry* entry)
{
    return entry->name[0] == '.';
}

static bool directory_is_empty(uint16_t first_cluster)
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
                return true;
            if (should_skip_entry(entry))
                continue;
            if (directory_entry_is_special(entry))
                continue;
            return false;
        }

        cluster = fat16_next_cluster(cluster);
    }

    return true;
}

static Fat16Result update_directory_parent(uint16_t dir_cluster, uint16_t parent_cluster)
{
    DirectoryEntry* entries;

    if (!read_cluster(dir_cluster))
        return FAT16_IO_ERROR;

    entries = (DirectoryEntry*)g_cluster_buffer;
    entries[1].first_cluster_low = parent_cluster;
    if (!write_cluster(dir_cluster))
        return FAT16_IO_ERROR;

    return FAT16_OK;
}

static Fat16Result write_data_to_chain(uint16_t first_cluster, uint32_t start_offset, const uint8_t* data, uint32_t size)
{
    uint32_t offset = start_offset;
    uint32_t remaining = size;
    const uint8_t* cursor = data;
    uint16_t cluster;

    if (size == 0u)
        return FAT16_OK;
    if (first_cluster < 2u)
        return FAT16_IO_ERROR;

    cluster = get_cluster_at_index(first_cluster, offset / g_cluster_size);
    offset %= g_cluster_size;
    if (cluster < 2u || cluster >= FAT16_END_OF_CHAIN)
        return FAT16_IO_ERROR;

    while (remaining != 0u) {
        uint32_t chunk;

        if (!read_cluster(cluster))
            return FAT16_IO_ERROR;

        chunk = g_cluster_size - offset;
        if (chunk > remaining)
            chunk = remaining;
        k_memcpy(g_cluster_buffer + offset, cursor, chunk);
        if (!write_cluster(cluster))
            return FAT16_IO_ERROR;

        remaining -= chunk;
        cursor += chunk;
        offset = 0u;
        if (remaining == 0u)
            break;

        cluster = fat16_next_cluster(cluster);
        if (cluster < 2u || cluster >= FAT16_END_OF_CHAIN)
            return FAT16_IO_ERROR;
    }

    return FAT16_OK;
}

static Fat16Result write_file_entry(const EntryRef* slot, const uint8_t fat_name[11], uint8_t attributes, uint16_t first_cluster, uint32_t size)
{
    DirectoryEntry entry;

    k_memset(&entry, 0, sizeof(entry));
    k_memcpy(entry.name, fat_name, 11u);
    entry.attributes = attributes;
    entry.first_cluster_low = first_cluster;
    entry.size = size;
    return write_entry_at(slot, &entry);
}

static void fill_special_dir_name(uint8_t out[11], const char* name)
{
    uint32_t i = 0;

    k_memset(out, ' ', 11u);
    while (name[i] != '\0' && i < 11u) {
        out[i] = (uint8_t)name[i];
        ++i;
    }
}

static Fat16Result lookup_parent_and_name(const char* path, DirectoryRef* parent_dir, uint8_t fat_name[11], char* parent_path_out)
{
    char name[13];
    Fat16Result result = split_path(path, parent_path_out, name);

    if (result != FAT16_OK)
        return result;
    if (!make_fat_name(name, fat_name))
        return FAT16_INVALID_NAME;
    return resolve_directory(parent_path_out, parent_dir);
}

static bool path_is_descendant_of(const char* path, const char* parent)
{
    uint32_t i = 0;

    while (parent[i] != '\0' && path[i] == parent[i])
        ++i;

    if (parent[i] != '\0')
        return false;
    if (parent[i - 1u] == '/')
        return true;
    return path[i] == '/' || path[i] == '\0';
}

bool fat16_mount(uint32_t partition_lba_start, uint16_t bytes_per_sector)
{
    const BootSector* boot_sector;
    uint32_t fat_bytes;
    uint32_t total_sectors;
    uint32_t data_sectors;

    if (bytes_per_sector != FAT16_SECTOR_SIZE)
        return false;
    if (!read_sector(partition_lba_start, g_sector_buffer))
        return false;
    boot_sector = (const BootSector*)g_sector_buffer;
    if (boot_sector->bytes_per_sector != FAT16_SECTOR_SIZE)
        return false;
    if (boot_sector->sectors_per_fat == 0 || boot_sector->sectors_per_fat > FAT16_MAX_FAT_SECTORS)
        return false;
    if (boot_sector->sectors_per_cluster == 0 || boot_sector->sectors_per_cluster > FAT16_MAX_CLUSTER_SECTORS)
        return false;

    total_sectors = boot_sector->total_sectors16 != 0 ? boot_sector->total_sectors16 : boot_sector->total_sectors32;
    if (total_sectors == 0u)
        return false;

    g_partition_lba = partition_lba_start;
    g_bytes_per_sector = boot_sector->bytes_per_sector;
    g_sectors_per_cluster = boot_sector->sectors_per_cluster;
    g_root_entry_count = boot_sector->dir_entry_count;
    g_fat_count = boot_sector->fat_count;
    g_reserved_sectors = boot_sector->reserved_sectors;
    g_sectors_per_fat = boot_sector->sectors_per_fat;
    g_root_sectors = ((uint32_t)g_root_entry_count * sizeof(DirectoryEntry) + (g_bytes_per_sector - 1u)) / g_bytes_per_sector;
    g_fat_lba = g_partition_lba + g_reserved_sectors;
    g_root_lba = g_fat_lba + (uint32_t)g_fat_count * g_sectors_per_fat;
    g_data_lba = g_root_lba + g_root_sectors;
    g_cluster_size = (uint32_t)g_sectors_per_cluster * g_bytes_per_sector;
    data_sectors = total_sectors - (g_reserved_sectors + (uint32_t)g_fat_count * g_sectors_per_fat + g_root_sectors);
    g_total_clusters = data_sectors / g_sectors_per_cluster + 2u;

    fat_bytes = (uint32_t)g_sectors_per_fat * g_bytes_per_sector;
    if (!block_read_sectors(g_fat_lba, (uint8_t)g_sectors_per_fat, g_fat))
        return false;

    k_memset(g_sector_buffer, 0, sizeof(g_sector_buffer));
    k_memset(g_cluster_buffer, 0, sizeof(g_cluster_buffer));
    k_memset(((uint8_t*)g_fat) + fat_bytes, 0, sizeof(g_fat) - fat_bytes);
    g_mounted = true;
    return true;
}

Fat16Result fat16_stat(const char* path, Fat16NodeInfo* out)
{
    ResolvedPath resolved;
    Fat16Result result;

    if (!g_mounted || path == NULL || out == NULL)
        return FAT16_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT16_OK)
        return result;

    if (resolved.is_root) {
        out->name[0] = '/';
        out->name[1] = '\0';
        out->is_dir = true;
        out->size = 0;
    } else {
        *out = decode_node_info(&resolved.entry);
    }

    return FAT16_OK;
}

static Fat16Result list_root_dir(Fat16ListCallback callback, void* context)
{
    uint32_t sector;

    for (sector = 0; sector < g_root_sectors; ++sector) {
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_sector(g_root_lba + sector, g_sector_buffer))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_sector_buffer;
        for (i = 0; i < FAT16_SECTOR_SIZE / sizeof(DirectoryEntry); ++i) {
            DirectoryEntry* entry = &entries[i];
            Fat16NodeInfo public_entry;

            if (entry->name[0] == 0x00)
                return FAT16_OK;
            if (should_skip_entry(entry))
                continue;

            public_entry = decode_node_info(entry);
            if (!callback(&public_entry, context))
                return FAT16_OK;
        }
    }

    return FAT16_OK;
}

static Fat16Result list_cluster_dir(uint16_t first_cluster, Fat16ListCallback callback, void* context)
{
    uint16_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t i;
        uint32_t entry_count;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return FAT16_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = g_cluster_size / sizeof(DirectoryEntry);
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];
            Fat16NodeInfo public_entry;

            if (entry->name[0] == 0x00)
                return FAT16_OK;
            if (should_skip_entry(entry))
                continue;
            if (entry->name[0] == '.')
                continue;

            public_entry = decode_node_info(entry);
            if (!callback(&public_entry, context))
                return FAT16_OK;
        }

        cluster = fat16_next_cluster(cluster);
    }

    return FAT16_OK;
}

Fat16Result fat16_list_dir(const char* path, Fat16ListCallback callback, void* context)
{
    ResolvedPath resolved;
    Fat16Result result;

    if (!g_mounted || path == NULL || callback == NULL)
        return FAT16_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT16_OK)
        return result;
    if (resolved.is_root)
        return list_root_dir(callback, context);
    if ((resolved.entry.attributes & FAT16_DIRECTORY) == 0)
        return FAT16_NOT_A_DIRECTORY;

    return list_cluster_dir(resolved.entry.first_cluster_low, callback, context);
}

Fat16Result fat16_read_file(const char* path, Fat16ReadCallback callback, void* context)
{
    ResolvedPath resolved;
    Fat16Result result;
    uint32_t remaining;
    uint16_t cluster;

    if (!g_mounted || path == NULL || callback == NULL)
        return FAT16_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT16_OK)
        return result;
    if (resolved.is_root)
        return FAT16_IS_A_DIRECTORY;
    if ((resolved.entry.attributes & FAT16_DIRECTORY) != 0)
        return FAT16_IS_A_DIRECTORY;

    remaining = resolved.entry.size;
    cluster = resolved.entry.first_cluster_low;

    while (remaining > 0 && cluster >= 2u && cluster < FAT16_END_OF_CHAIN) {
        uint32_t chunk = g_cluster_size;

        if (!read_cluster(cluster))
            return FAT16_IO_ERROR;
        if (chunk > remaining)
            chunk = remaining;
        if (!callback(g_cluster_buffer, chunk, context))
            return FAT16_IO_ERROR;

        remaining -= chunk;
        cluster = fat16_next_cluster(cluster);
    }

    return remaining == 0u ? FAT16_OK : FAT16_IO_ERROR;
}

Fat16Result fat16_write_file(const char* path, const uint8_t* data, uint32_t size, bool append)
{
    DirectoryRef parent_dir;
    uint8_t fat_name[11];
    char parent_path[128];
    EntryRef entry_ref;
    EntryRef slot;
    DirectoryEntry entry;
    Fat16Result result;
    uint32_t old_size = 0u;
    uint16_t first_cluster = 0u;
    uint32_t current_clusters = 0u;
    uint32_t required_clusters = 0u;

    if (!g_mounted || path == NULL)
        return FAT16_IO_ERROR;
    if (size != 0u && data == NULL)
        return FAT16_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, fat_name, parent_path);
    if (result != FAT16_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, fat_name, &entry_ref);
    if (result == FAT16_OK) {
        entry = entry_ref.entry;
        if ((entry.attributes & FAT16_DIRECTORY) != 0)
            return FAT16_IS_A_DIRECTORY;
        old_size = entry.size;
        slot = entry_ref;
        current_clusters = clusters_for_size(old_size);
        first_cluster = entry.first_cluster_low;
    } else if (result == FAT16_NOT_FOUND) {
        k_memset(&entry, 0, sizeof(entry));
        k_memcpy(entry.name, fat_name, 11u);
        entry.attributes = 0x20u;
        result = find_free_entry_in_directory(&parent_dir, &slot);
        if (result != FAT16_OK)
            return result;
        first_cluster = 0u;
    } else {
        return result;
    }

    if (!append) {
        required_clusters = clusters_for_size(size);
        result = ensure_cluster_chain_length(&first_cluster, current_clusters, required_clusters);
        if (result != FAT16_OK)
            return result;
        result = trim_cluster_chain(&first_cluster, required_clusters);
        if (result != FAT16_OK)
            return result;
        if (!flush_fat())
            return FAT16_IO_ERROR;

        result = write_data_to_chain(first_cluster, 0u, data, size);
        if (result != FAT16_OK)
            return result;

        entry.first_cluster_low = first_cluster;
        entry.size = size;
        return write_entry_at(&slot, &entry);
    }

    required_clusters = clusters_for_size(old_size + size);
    result = ensure_cluster_chain_length(&first_cluster, current_clusters, required_clusters);
    if (result != FAT16_OK)
        return result;
    if (!flush_fat())
        return FAT16_IO_ERROR;
    result = write_data_to_chain(first_cluster, old_size, data, size);
    if (result != FAT16_OK)
        return result;

    entry.first_cluster_low = first_cluster;
    entry.size = old_size + size;
    return write_entry_at(&slot, &entry);
}

Fat16Result fat16_make_dir(const char* path)
{
    DirectoryRef parent_dir;
    uint8_t fat_name[11];
    char parent_path[128];
    EntryRef existing;
    EntryRef slot;
    uint16_t dir_cluster = 0u;
    DirectoryEntry* entries;
    Fat16Result result;

    if (!g_mounted || path == NULL)
        return FAT16_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, fat_name, parent_path);
    if (result != FAT16_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, fat_name, &existing);
    if (result == FAT16_OK)
        return FAT16_ALREADY_EXISTS;
    if (result != FAT16_NOT_FOUND)
        return result;

    result = find_free_entry_in_directory(&parent_dir, &slot);
    if (result != FAT16_OK)
        return result;

    result = allocate_cluster_chain(1u, &dir_cluster);
    if (result != FAT16_OK)
        return result;
    if (!flush_fat())
        return FAT16_IO_ERROR;

    k_memset(g_cluster_buffer, 0, g_cluster_size);
    entries = (DirectoryEntry*)g_cluster_buffer;
    k_memset(&entries[0], 0, sizeof(DirectoryEntry));
    fill_special_dir_name(entries[0].name, ".");
    entries[0].attributes = FAT16_DIRECTORY;
    entries[0].first_cluster_low = dir_cluster;
    k_memset(&entries[1], 0, sizeof(DirectoryEntry));
    fill_special_dir_name(entries[1].name, "..");
    entries[1].attributes = FAT16_DIRECTORY;
    entries[1].first_cluster_low = parent_dir.is_root ? 0u : parent_dir.first_cluster;
    if (!write_cluster(dir_cluster))
        return FAT16_IO_ERROR;

    return write_file_entry(&slot, fat_name, FAT16_DIRECTORY, dir_cluster, 0u);
}

Fat16Result fat16_remove(const char* path)
{
    DirectoryRef parent_dir;
    uint8_t fat_name[11];
    char parent_path[128];
    EntryRef entry_ref;
    Fat16Result result;

    if (!g_mounted || path == NULL)
        return FAT16_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, fat_name, parent_path);
    if (result != FAT16_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, fat_name, &entry_ref);
    if (result != FAT16_OK)
        return result;

    if ((entry_ref.entry.attributes & FAT16_DIRECTORY) != 0) {
        if (!directory_is_empty(entry_ref.entry.first_cluster_low))
            return FAT16_DIRECTORY_NOT_EMPTY;
    }

    if (entry_ref.entry.first_cluster_low >= 2u)
        free_cluster_chain_in_memory(entry_ref.entry.first_cluster_low);
    if (!flush_fat())
        return FAT16_IO_ERROR;

    return mark_entry_deleted(&entry_ref);
}

Fat16Result fat16_rename(const char* old_path, const char* new_path)
{
    DirectoryRef old_parent_dir;
    DirectoryRef new_parent_dir;
    uint8_t old_name[11];
    uint8_t new_name[11];
    char old_parent_path[128];
    char new_parent_path[128];
    EntryRef old_ref;
    EntryRef existing_ref;
    EntryRef new_slot;
    Fat16Result result;

    if (!g_mounted || old_path == NULL || new_path == NULL)
        return FAT16_IO_ERROR;
    if (k_strcmp(old_path, new_path) == 0)
        return FAT16_OK;

    result = lookup_parent_and_name(old_path, &old_parent_dir, old_name, old_parent_path);
    if (result != FAT16_OK)
        return result;
    result = lookup_parent_and_name(new_path, &new_parent_dir, new_name, new_parent_path);
    if (result != FAT16_OK)
        return result;

    result = find_entry_in_directory(&old_parent_dir, old_name, &old_ref);
    if (result != FAT16_OK)
        return result;

    if ((old_ref.entry.attributes & FAT16_DIRECTORY) != 0 && path_is_descendant_of(new_parent_path, old_path))
        return FAT16_INVALID_NAME;

    result = find_entry_in_directory(&new_parent_dir, new_name, &existing_ref);
    if (result == FAT16_OK)
        return FAT16_ALREADY_EXISTS;
    if (result != FAT16_NOT_FOUND)
        return result;

    if (old_parent_dir.is_root == new_parent_dir.is_root && old_parent_dir.first_cluster == new_parent_dir.first_cluster) {
        k_memcpy(old_ref.entry.name, new_name, 11u);
        return write_entry_at(&old_ref, &old_ref.entry);
    }

    result = find_free_entry_in_directory(&new_parent_dir, &new_slot);
    if (result != FAT16_OK)
        return result;

    k_memcpy(old_ref.entry.name, new_name, 11u);
    result = write_entry_at(&new_slot, &old_ref.entry);
    if (result != FAT16_OK)
        return result;
    result = mark_entry_deleted(&old_ref);
    if (result != FAT16_OK)
        return result;

    if ((old_ref.entry.attributes & FAT16_DIRECTORY) != 0)
        return update_directory_parent(old_ref.entry.first_cluster_low, new_parent_dir.is_root ? 0u : new_parent_dir.first_cluster);

    return FAT16_OK;
}
