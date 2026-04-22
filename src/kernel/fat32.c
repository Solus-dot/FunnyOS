#include "fat32.h"
#include "block.h"
#include "kstring.h"

#define FAT32_MAX_FAT_SECTORS 4096u
#define FAT32_MAX_CLUSTER_SECTORS 8u
#define FAT32_SECTOR_SIZE 512u
#define FAT32_END_OF_CHAIN 0x0FFFFFF8u
#define FAT32_DIRECTORY 0x10u
#define FAT32_VOLUME_ID 0x08u
#define FAT32_LFN 0x0Fu
#define FAT32_ARCHIVE 0x20u
#define FAT32_DELETED 0xE5u
#define FAT32_LFN_LAST 0x40u
#define FAT32_LFN_ENTRY_MASK 0x1Fu
#define FAT32_MAX_NAME_ENTRIES 11u

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
    uint16_t sectors_per_fat16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t sectors_per_fat32;
    uint16_t extended_flags;
    uint16_t filesystem_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved0[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t system_id[8];
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

typedef struct __attribute__((packed)) LfnDirectoryEntry {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attributes;
    uint8_t entry_type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_low;
    uint16_t name3[2];
} LfnDirectoryEntry;

typedef struct ResolvedPath {
    bool is_root;
    DirectoryEntry entry;
} ResolvedPath;

typedef struct DirectoryRef {
    bool is_root;
    uint32_t first_cluster;
} DirectoryRef;

typedef struct EntryRef {
    bool found;
    uint32_t directory_first_cluster;
    uint32_t entry_index;
    uint32_t lfn_entry_count;
    DirectoryEntry entry;
} EntryRef;

typedef struct LfnState {
    bool active;
    bool valid;
    uint8_t checksum;
    uint8_t expected_order;
    char name[FAT32_NAME_CAPACITY];
} LfnState;

static uint32_t entries_per_cluster(void);
static Fat32Result find_entry_in_cluster_dir(uint32_t first_cluster, const char* name, EntryRef* out);
static Fat32Result find_free_entry_in_cluster_dir(uint32_t first_cluster, uint32_t needed_entries, EntryRef* out);
static Fat32Result list_cluster_dir(uint32_t first_cluster, Fat32ListCallback callback, void* context);
static bool is_valid_data_cluster(uint32_t cluster);

static uint32_t entry_first_cluster(const DirectoryEntry* entry)
{
    return ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

static void entry_set_first_cluster(DirectoryEntry* entry, uint32_t cluster)
{
    entry->first_cluster_low = (uint16_t)(cluster & 0xFFFFu);
    entry->first_cluster_high = (uint16_t)((cluster >> 16) & 0xFFFFu);
}

static uint32_t g_fat[FAT32_MAX_FAT_SECTORS * FAT32_SECTOR_SIZE / 4u];
static uint8_t g_sector_buffer[FAT32_SECTOR_SIZE];
static uint8_t g_cluster_buffer[FAT32_MAX_CLUSTER_SECTORS * FAT32_SECTOR_SIZE];

static bool g_mounted = false;
static uint16_t g_bytes_per_sector = 0;
static uint8_t g_sectors_per_cluster = 0;
static uint8_t g_fat_count = 0;
static uint16_t g_reserved_sectors = 0;
static uint32_t g_sectors_per_fat = 0;
static uint32_t g_partition_lba = 0;
static uint32_t g_root_cluster = 0;
static uint32_t g_data_lba = 0;
static uint32_t g_cluster_size = 0;
static uint32_t g_total_clusters = 0;
static uint32_t g_fat_lba = 0;

static bool read_sector(uint32_t lba, void* out)
{
    return block_read_sectors(lba, 1, out);
}

static bool read_sectors(uint32_t lba, uint32_t count, void* out)
{
    uint8_t* cursor = (uint8_t*)out;

    while (count != 0u) {
        uint8_t chunk = count > 255u ? 255u : (uint8_t)count;

        if (!block_read_sectors(lba, chunk, cursor))
            return false;
        lba += chunk;
        count -= chunk;
        cursor += (uint32_t)chunk * FAT32_SECTOR_SIZE;
    }

    return true;
}

static bool write_sectors(uint32_t lba, uint32_t count, const void* data)
{
    const uint8_t* cursor = (const uint8_t*)data;

    while (count != 0u) {
        uint8_t chunk = count > 255u ? 255u : (uint8_t)count;

        if (!block_write_sectors(lba, chunk, cursor))
            return false;
        lba += chunk;
        count -= chunk;
        cursor += (uint32_t)chunk * FAT32_SECTOR_SIZE;
    }

    return true;
}

static uint32_t fat32_next_cluster(uint32_t cluster)
{
    if (!is_valid_data_cluster(cluster))
        return FAT32_END_OF_CHAIN;

    return g_fat[cluster] & 0x0FFFFFFFu;
}

static void fat32_set_cluster(uint32_t cluster, uint32_t value)
{
    if (!is_valid_data_cluster(cluster))
        return;

    g_fat[cluster] = value & 0x0FFFFFFFu;
}

static uint32_t cluster_to_lba(uint32_t cluster)
{
    return g_data_lba + (uint32_t)(cluster - 2u) * g_sectors_per_cluster;
}

static bool is_valid_data_cluster(uint32_t cluster)
{
    return cluster >= 2u && cluster < g_total_clusters;
}

static bool read_cluster(uint32_t cluster)
{
    if (g_sectors_per_cluster == 0 || g_sectors_per_cluster > FAT32_MAX_CLUSTER_SECTORS)
        return false;
    if (!is_valid_data_cluster(cluster))
        return false;

    return block_read_sectors(cluster_to_lba(cluster), g_sectors_per_cluster, g_cluster_buffer);
}

static bool write_cluster(uint32_t cluster)
{
    if (g_sectors_per_cluster == 0 || g_sectors_per_cluster > FAT32_MAX_CLUSTER_SECTORS)
        return false;
    if (!is_valid_data_cluster(cluster))
        return false;

    return block_write_sectors(cluster_to_lba(cluster), g_sectors_per_cluster, g_cluster_buffer);
}

static bool flush_fat(void)
{
    uint32_t i;

    for (i = 0; i < g_fat_count; ++i) {
        if (!write_sectors(g_fat_lba + i * g_sectors_per_fat, g_sectors_per_fat, g_fat))
            return false;
    }

    return true;
}

static bool should_skip_entry(const DirectoryEntry* entry)
{
    if (entry->name[0] == 0x00 || entry->name[0] == FAT32_DELETED)
        return true;
    if ((entry->attributes & FAT32_VOLUME_ID) != 0)
        return true;
    return false;
}

static void format_short_name(const uint8_t raw[11], char out[FAT32_NAME_CAPACITY])
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

static bool is_invalid_lfn_character(char c)
{
    return c < 0x20 || c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' || c == '\\' || c == '|';
}

static bool validate_long_name(const char* name, uint32_t* length_out)
{
    uint32_t length = 0;

    if (name == NULL || *name == '\0')
        return false;
    if ((name[0] == '.' && name[1] == '\0') || (name[0] == '.' && name[1] == '.' && name[2] == '\0'))
        return false;

    while (name[length] != '\0') {
        if (length + 1u >= FAT32_NAME_CAPACITY)
            return false;
        if (is_invalid_lfn_character(name[length]))
            return false;
        ++length;
    }
    if (length == 0u)
        return false;
    if (name[length - 1u] == ' ' || name[length - 1u] == '.')
        return false;

    if (length_out != NULL)
        *length_out = length;
    return true;
}

static bool name_equals_ignore_case(const char* lhs, const char* rhs)
{
    uint32_t i = 0;

    while (lhs[i] != '\0' && rhs[i] != '\0') {
        if (k_toupper(lhs[i]) != k_toupper(rhs[i]))
            return false;
        ++i;
    }

    return lhs[i] == '\0' && rhs[i] == '\0';
}

static uint8_t short_name_checksum(const uint8_t name[11])
{
    uint32_t i;
    uint8_t sum = 0;

    for (i = 0; i < 11u; ++i)
        sum = (uint8_t)(((sum & 1u) != 0u ? 0x80u : 0u) + (sum >> 1u) + name[i]);

    return sum;
}

static void lfn_state_reset(LfnState* state)
{
    state->active = false;
    state->valid = false;
    state->checksum = 0u;
    state->expected_order = 0u;
    state->name[0] = '\0';
}

static bool decode_lfn_character(uint16_t value, char* out)
{
    if (value == 0x0000u || value == 0xFFFFu) {
        *out = '\0';
        return true;
    }
    if ((value & 0xFF00u) != 0u)
        return false;

    *out = (char)(value & 0x00FFu);
    return !is_invalid_lfn_character(*out);
}

static void lfn_store_character(char* name, uint32_t index, char value)
{
    if (index + 1u < FAT32_NAME_CAPACITY)
        name[index] = value;
}

static void lfn_terminate_name(char* name)
{
    uint32_t i;

    for (i = 0; i < FAT32_NAME_CAPACITY; ++i) {
        if (name[i] == '\0')
            return;
    }
    name[FAT32_NAME_CAPACITY - 1u] = '\0';
}

static void process_lfn_entry(const DirectoryEntry* entry, LfnState* state)
{
    const LfnDirectoryEntry* lfn = (const LfnDirectoryEntry*)entry;
    uint8_t order = lfn->order & FAT32_LFN_ENTRY_MASK;
    uint32_t base_index;
    uint32_t i;

    if ((lfn->order & FAT32_LFN_LAST) != 0u) {
        lfn_state_reset(state);
        if (order == 0u)
            return;
        state->active = true;
        state->valid = true;
        state->checksum = lfn->checksum;
        state->expected_order = order;
        k_memset(state->name, 0, sizeof(state->name));
    } else if (!state->active || !state->valid || lfn->checksum != state->checksum || order == 0u || order != state->expected_order - 1u) {
        lfn_state_reset(state);
        return;
    } else {
        state->expected_order = order;
    }

    base_index = (uint32_t)(order - 1u) * 13u;
    for (i = 0; i < 5u; ++i) {
        char decoded;

        if (!decode_lfn_character(lfn->name1[i], &decoded)) {
            state->valid = false;
            return;
        }
        lfn_store_character(state->name, base_index + i, decoded);
    }
    for (i = 0; i < 6u; ++i) {
        char decoded;

        if (!decode_lfn_character(lfn->name2[i], &decoded)) {
            state->valid = false;
            return;
        }
        lfn_store_character(state->name, base_index + 5u + i, decoded);
    }
    for (i = 0; i < 2u; ++i) {
        char decoded;

        if (!decode_lfn_character(lfn->name3[i], &decoded)) {
            state->valid = false;
            return;
        }
        lfn_store_character(state->name, base_index + 11u + i, decoded);
    }
    lfn_terminate_name(state->name);
}

static void decode_directory_entry_name(const DirectoryEntry* entry, const LfnState* lfn, char out[FAT32_NAME_CAPACITY])
{
    if (lfn->active && lfn->valid && lfn->expected_order == 1u && lfn->checksum == short_name_checksum(entry->name) && lfn->name[0] != '\0') {
        k_strcpy(out, lfn->name);
        return;
    }

    format_short_name(entry->name, out);
}

static bool make_short_name_if_possible(const char* name, uint8_t out[11], bool* exact_out)
{
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    uint32_t last_dot = 0xFFFFFFFFu;
    uint32_t i;
    uint32_t length;

    if (!validate_long_name(name, &length))
        return false;

    for (i = 0; i < length; ++i) {
        if (name[i] == '.')
            last_dot = i;
    }
    if (last_dot == 0u || last_dot == length - 1u)
        last_dot = 0xFFFFFFFFu;

    k_memset(out, ' ', 11u);
    for (i = 0; i < length; ++i) {
        char c = name[i];

        if (c == '.') {
            if (last_dot == 0xFFFFFFFFu || i != last_dot)
                return false;
            continue;
        }
        if (k_is_space(c) || c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']')
            return false;

        if (last_dot == 0xFFFFFFFFu || i < last_dot) {
            if (base_len >= 8u)
                return false;
            out[base_len++] = (uint8_t)k_toupper(c);
        } else {
            if (ext_len >= 3u)
                return false;
            out[8u + ext_len++] = (uint8_t)k_toupper(c);
        }
    }
    if (base_len == 0u)
        return false;

    if (exact_out != NULL)
        *exact_out = true;
    return true;
}

static void build_short_alias(const char* name, uint32_t ordinal, uint8_t out[11])
{
    char base[9];
    char ext[4];
    char digits[10];
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    uint32_t digits_len = 0;
    uint32_t last_dot = 0xFFFFFFFFu;
    uint32_t i;
    uint32_t name_len;
    uint32_t copy_len;

    k_memset(out, ' ', 11u);
    k_memset(base, 0, sizeof(base));
    k_memset(ext, 0, sizeof(ext));

    name_len = (uint32_t)k_strlen(name);
    for (i = 0; i < name_len; ++i) {
        if (name[i] == '.')
            last_dot = i;
    }
    if (last_dot == 0u || last_dot == name_len - 1u)
        last_dot = 0xFFFFFFFFu;

    for (i = 0; i < name_len; ++i) {
        char c = name[i];

        if (c == '.')
            continue;
        if (is_invalid_lfn_character(c) || k_is_space(c))
            continue;

        if (last_dot != 0xFFFFFFFFu && i > last_dot) {
            if (ext_len < 3u)
                ext[ext_len++] = k_toupper(c);
        } else if (base_len < 8u) {
            base[base_len++] = k_toupper(c);
        }
    }
    if (base_len == 0u) {
        base[base_len++] = 'F';
    }

    do {
        digits[digits_len++] = (char)('0' + (ordinal % 10u));
        ordinal /= 10u;
    } while (ordinal != 0u && digits_len < sizeof(digits));

    copy_len = 8u - (digits_len + 1u);
    if (copy_len > base_len)
        copy_len = base_len;

    for (i = 0; i < copy_len; ++i)
        out[i] = (uint8_t)base[i];
    out[copy_len++] = '~';
    while (digits_len != 0u)
        out[copy_len++] = (uint8_t)digits[--digits_len];
    for (i = 0; i < ext_len; ++i)
        out[8u + i] = (uint8_t)ext[i];
}

static bool short_name_exists_in_directory(uint32_t first_cluster, const uint8_t short_name[11], const uint8_t ignore_short_name[11])
{
    uint32_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t i;
        uint32_t entry_count;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return true;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = entries_per_cluster();
        for (i = 0; i < entry_count; ++i) {
            if (entries[i].name[0] == 0x00)
                return false;
            if (entries[i].name[0] == FAT32_DELETED || entries[i].attributes == FAT32_LFN)
                continue;
            if ((entries[i].attributes & FAT32_VOLUME_ID) != 0)
                continue;
            if (ignore_short_name != NULL && k_memcmp(entries[i].name, ignore_short_name, 11u) == 0)
                continue;
            if (k_memcmp(entries[i].name, short_name, 11u) == 0)
                return true;
        }

        cluster = fat32_next_cluster(cluster);
    }

    return false;
}

static uint32_t lfn_entry_count_for_name(const char* name)
{
    uint32_t length = (uint32_t)k_strlen(name);

    return (length + 12u) / 13u;
}

static void fill_lfn_name_segment(LfnDirectoryEntry* lfn, const char* long_name, uint32_t offset, uint32_t total_length)
{
    uint32_t index = 0u;
    uint32_t i;

    for (i = 0; i < 5u; ++i, ++index) {
        uint32_t source_index = offset + index;

        lfn->name1[i] = source_index < total_length ? (uint16_t)(uint8_t)long_name[source_index] : (source_index == total_length ? 0x0000u : 0xFFFFu);
    }
    for (i = 0; i < 6u; ++i, ++index) {
        uint32_t source_index = offset + index;

        lfn->name2[i] = source_index < total_length ? (uint16_t)(uint8_t)long_name[source_index] : (source_index == total_length ? 0x0000u : 0xFFFFu);
    }
    for (i = 0; i < 2u; ++i, ++index) {
        uint32_t source_index = offset + index;

        lfn->name3[i] = source_index < total_length ? (uint16_t)(uint8_t)long_name[source_index] : (source_index == total_length ? 0x0000u : 0xFFFFu);
    }
}

static Fat32Result build_name_entries(uint32_t directory_cluster, const char* name, uint8_t attributes, uint32_t first_cluster, uint32_t size, const uint8_t ignore_short_name[11], DirectoryEntry entries_out[FAT32_MAX_NAME_ENTRIES], uint32_t* entry_count_out)
{
    uint8_t short_name[11];
    uint8_t checksum;
    bool exact_short = false;
    uint32_t lfn_count = 0u;
    uint32_t total_entries;
    uint32_t ordinal = 1u;
    uint32_t i;

    if (!validate_long_name(name, NULL))
        return FAT32_INVALID_NAME;

    if (!make_short_name_if_possible(name, short_name, &exact_short)) {
        exact_short = false;
    }
    if (exact_short && short_name_exists_in_directory(directory_cluster, short_name, ignore_short_name))
        exact_short = false;
    if (!exact_short) {
        do {
            build_short_alias(name, ordinal++, short_name);
        } while (short_name_exists_in_directory(directory_cluster, short_name, ignore_short_name));
        lfn_count = lfn_entry_count_for_name(name);
    }

    total_entries = lfn_count + 1u;
    if (total_entries > FAT32_MAX_NAME_ENTRIES)
        return FAT32_INVALID_NAME;

    checksum = short_name_checksum(short_name);
    for (i = 0; i < lfn_count; ++i) {
        LfnDirectoryEntry* lfn = (LfnDirectoryEntry*)&entries_out[i];
        uint32_t order = lfn_count - i;
        uint8_t order_byte = (uint8_t)order;

        if (i == 0u)
            order_byte |= FAT32_LFN_LAST;
        k_memset(lfn, 0, sizeof(*lfn));
        lfn->order = order_byte;
        lfn->attributes = FAT32_LFN;
        lfn->entry_type = 0u;
        lfn->checksum = checksum;
        lfn->first_cluster_low = 0u;
        fill_lfn_name_segment(lfn, name, (order - 1u) * 13u, (uint32_t)k_strlen(name));
    }

    k_memset(&entries_out[lfn_count], 0, sizeof(DirectoryEntry));
    k_memcpy(entries_out[lfn_count].name, short_name, 11u);
    entries_out[lfn_count].attributes = attributes;
    entry_set_first_cluster(&entries_out[lfn_count], first_cluster);
    entries_out[lfn_count].size = size;
    *entry_count_out = total_entries;
    return FAT32_OK;
}

static Fat32NodeInfo decode_node_info(const DirectoryEntry* entry, const char* name)
{
    Fat32NodeInfo out;

    k_strcpy(out.name, name);
    out.is_dir = (entry->attributes & FAT32_DIRECTORY) != 0;
    out.size = entry->size;
    return out;
}

static Fat32Result split_path(const char* path, char* parent_out, char* name_out)
{
    const char* cursor;
    const char* last_slash = NULL;
    uint32_t parent_len = 0;
    uint32_t name_len = 0;

    if (path == NULL || parent_out == NULL || name_out == NULL)
        return FAT32_INVALID_NAME;
    if (path[0] != '/' || path[1] == '\0')
        return FAT32_INVALID_NAME;

    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/')
            last_slash = cursor;
    }

    if (last_slash == NULL || last_slash[1] == '\0')
        return FAT32_INVALID_NAME;

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
        if (name_len + 1u >= FAT32_NAME_CAPACITY)
            return FAT32_INVALID_NAME;
        name_out[name_len] = cursor[name_len];
        ++name_len;
    }
    name_out[name_len] = '\0';
    return FAT32_OK;
}

static Fat32Result resolve_path(const char* path, ResolvedPath* out)
{
    DirectoryEntry current;
    bool have_current = false;
    const char* cursor = path;

    while (*cursor == '/')
        ++cursor;

    if (*cursor == '\0') {
        out->is_root = true;
        return FAT32_OK;
    }

    k_memset(&current, 0, sizeof(current));
    while (*cursor != '\0') {
        char component[FAT32_NAME_CAPACITY];
        const char* slash = cursor;
        uint32_t len = 0;
        DirectoryRef directory;
        Fat32Result result;

        while (*slash != '\0' && *slash != '/') {
            if (len + 1u >= sizeof(component))
                return FAT32_INVALID_NAME;
            component[len++] = *slash;
            ++slash;
        }
        component[len] = '\0';
        if (!validate_long_name(component, NULL))
            return FAT32_INVALID_NAME;

        directory.is_root = !have_current;
        directory.first_cluster = have_current ? entry_first_cluster(&current) : g_root_cluster;
        if (have_current && (current.attributes & FAT32_DIRECTORY) == 0)
            return FAT32_NOT_A_DIRECTORY;

        {
            EntryRef found_ref;

            result = find_entry_in_cluster_dir(directory.first_cluster, component, &found_ref);
            if (result != FAT32_OK)
                return result;
            current = found_ref.entry;
            have_current = true;
        }

        while (*slash == '/')
            ++slash;
        if (*slash != '\0' && (current.attributes & FAT32_DIRECTORY) == 0)
            return FAT32_NOT_A_DIRECTORY;

        cursor = slash;
    }

    out->is_root = false;
    out->entry = current;
    return FAT32_OK;
}

static Fat32Result resolve_directory(const char* path, DirectoryRef* out)
{
    ResolvedPath resolved;
    Fat32Result result;

    result = resolve_path(path, &resolved);
    if (result != FAT32_OK)
        return result;

    if (resolved.is_root) {
        out->is_root = true;
        out->first_cluster = g_root_cluster;
        return FAT32_OK;
    }
    if ((resolved.entry.attributes & FAT32_DIRECTORY) == 0)
        return FAT32_NOT_A_DIRECTORY;

    out->is_root = false;
    out->first_cluster = entry_first_cluster(&resolved.entry);
    return FAT32_OK;
}

static uint32_t entries_per_cluster(void)
{
    return g_cluster_size / sizeof(DirectoryEntry);
}

static uint32_t cluster_for_directory_index(uint32_t first_cluster, uint32_t entry_index, uint32_t* entry_index_in_cluster_out)
{
    uint32_t cluster = first_cluster;
    uint32_t cluster_offset = entry_index / entries_per_cluster();

    while (cluster_offset != 0u && cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        cluster = fat32_next_cluster(cluster);
        --cluster_offset;
    }
    if (entry_index_in_cluster_out != NULL)
        *entry_index_in_cluster_out = entry_index % entries_per_cluster();

    return cluster;
}

static Fat32Result read_directory_entry(uint32_t first_cluster, uint32_t entry_index, DirectoryEntry* out)
{
    uint32_t index_in_cluster;
    uint32_t cluster = cluster_for_directory_index(first_cluster, entry_index, &index_in_cluster);

    if (cluster < 2u || cluster >= FAT32_END_OF_CHAIN)
        return FAT32_NOT_FOUND;
    if (!read_cluster(cluster))
        return FAT32_IO_ERROR;

    *out = ((DirectoryEntry*)g_cluster_buffer)[index_in_cluster];
    return FAT32_OK;
}

static Fat32Result write_directory_entry(uint32_t first_cluster, uint32_t entry_index, const DirectoryEntry* entry)
{
    uint32_t index_in_cluster;
    uint32_t cluster = cluster_for_directory_index(first_cluster, entry_index, &index_in_cluster);

    if (cluster < 2u || cluster >= FAT32_END_OF_CHAIN)
        return FAT32_IO_ERROR;
    if (!read_cluster(cluster))
        return FAT32_IO_ERROR;

    ((DirectoryEntry*)g_cluster_buffer)[index_in_cluster] = *entry;
    if (!write_cluster(cluster))
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

static Fat32Result find_entry_in_cluster_dir(uint32_t first_cluster, const char* name, EntryRef* out)
{
    uint32_t cluster = first_cluster;
    uint32_t entry_base_index = 0u;
    LfnState lfn;

    out->found = false;
    lfn_state_reset(&lfn);
    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
                    return FAT32_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = entries_per_cluster();
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];
            char entry_name[FAT32_NAME_CAPACITY];

            if (entry->name[0] == 0x00)
                return FAT32_NOT_FOUND;
            if (entry->name[0] == FAT32_DELETED) {
                lfn_state_reset(&lfn);
                continue;
            }
            if (entry->attributes == FAT32_LFN) {
                process_lfn_entry(entry, &lfn);
                continue;
            }
            if (should_skip_entry(entry)) {
                lfn_state_reset(&lfn);
                continue;
            }

            decode_directory_entry_name(entry, &lfn, entry_name);
            if (name_equals_ignore_case(entry_name, name)) {
                uint32_t j = entry_base_index + i;

                out->found = true;
                out->directory_first_cluster = first_cluster;
                out->entry_index = j;
                out->entry = *entry;
                out->lfn_entry_count = 0u;
                if (lfn.active && lfn.valid && lfn.expected_order == 1u && lfn.checksum == short_name_checksum(entry->name)) {
                    while (j != 0u) {
                        DirectoryEntry previous;
                        Fat32Result result = read_directory_entry(first_cluster, j - 1u, &previous);

                        if (result != FAT32_OK || previous.attributes != FAT32_LFN)
                            break;
                        ++out->lfn_entry_count;
                        --j;
                    }
                } else {
                    out->lfn_entry_count = 0u;
                }
                return FAT32_OK;
            }
            lfn_state_reset(&lfn);
        }

        entry_base_index += entry_count;
        cluster = fat32_next_cluster(cluster);
    }

    return FAT32_NOT_FOUND;
}

static Fat32Result find_entry_in_directory(const DirectoryRef* directory, const char* name, EntryRef* out)
{
    return find_entry_in_cluster_dir(directory->first_cluster, name, out);
}

static uint32_t find_free_cluster(void)
{
    uint32_t cluster;

    for (cluster = 2u; cluster < g_total_clusters; ++cluster) {
        if (g_fat[cluster] == 0u)
            return cluster;
    }

    return 0u;
}

static void free_cluster_chain_in_memory(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;

    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t next = fat32_next_cluster(cluster);
        fat32_set_cluster(cluster, 0u);
        cluster = next;
    }
}

static Fat32Result trim_cluster_chain(uint32_t* first_cluster, uint32_t required_count)
{
    uint32_t cluster;
    uint32_t next;

    if (first_cluster == NULL)
        return FAT32_IO_ERROR;

    if (required_count == 0u) {
        if (*first_cluster >= 2u)
            free_cluster_chain_in_memory(*first_cluster);
        *first_cluster = 0u;
        return FAT32_OK;
    }
    if (*first_cluster < 2u)
        return FAT32_OK;

    cluster = *first_cluster;
    while (required_count > 1u && cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        cluster = fat32_next_cluster(cluster);
        --required_count;
    }

    if (cluster < 2u || cluster >= FAT32_END_OF_CHAIN)
        return FAT32_IO_ERROR;

    next = fat32_next_cluster(cluster);
    fat32_set_cluster(cluster, FAT32_END_OF_CHAIN);
    if (next >= 2u && next < FAT32_END_OF_CHAIN)
        free_cluster_chain_in_memory(next);

    return FAT32_OK;
}

static Fat32Result append_cluster_to_chain(uint32_t first_cluster, uint32_t* new_cluster_out)
{
    uint32_t cluster;
    uint32_t next;
    uint32_t free_cluster = find_free_cluster();

    if (free_cluster == 0u)
        return FAT32_NO_SPACE;

    fat32_set_cluster(free_cluster, FAT32_END_OF_CHAIN);
    if (first_cluster == 0u) {
        *new_cluster_out = free_cluster;
        return FAT32_OK;
    }

    cluster = first_cluster;
    next = fat32_next_cluster(cluster);
    while (next >= 2u && next < FAT32_END_OF_CHAIN) {
        cluster = next;
        next = fat32_next_cluster(cluster);
    }

    fat32_set_cluster(cluster, free_cluster);
    *new_cluster_out = free_cluster;
    return FAT32_OK;
}

static Fat32Result zero_cluster(uint32_t cluster)
{
    k_memset(g_cluster_buffer, 0, g_cluster_size);
    return write_cluster(cluster) ? FAT32_OK : FAT32_IO_ERROR;
}

static Fat32Result allocate_cluster_chain(uint32_t count, uint32_t* first_cluster_out)
{
    uint32_t first_cluster = 0u;
    uint32_t last_cluster = 0u;
    uint32_t i;

    if (count == 0u) {
        *first_cluster_out = 0u;
        return FAT32_OK;
    }

    for (i = 0; i < count; ++i) {
        uint32_t cluster = find_free_cluster();

        if (cluster == 0u) {
            free_cluster_chain_in_memory(first_cluster);
            return FAT32_NO_SPACE;
        }

        fat32_set_cluster(cluster, FAT32_END_OF_CHAIN);
        if (last_cluster != 0u)
            fat32_set_cluster(last_cluster, cluster);
        else
            first_cluster = cluster;
        last_cluster = cluster;
    }

    for (i = 0, last_cluster = first_cluster; i < count && last_cluster >= 2u && last_cluster < FAT32_END_OF_CHAIN; ++i) {
        Fat32Result result = zero_cluster(last_cluster);
        uint32_t next = fat32_next_cluster(last_cluster);

        if (result != FAT32_OK) {
            free_cluster_chain_in_memory(first_cluster);
            return result;
        }
        if (next >= FAT32_END_OF_CHAIN)
            break;
        last_cluster = next;
    }

    *first_cluster_out = first_cluster;
    return FAT32_OK;
}

static uint32_t clusters_for_size(uint32_t size)
{
    if (size == 0u)
        return 0u;
    return (size + g_cluster_size - 1u) / g_cluster_size;
}

static uint32_t get_cluster_at_index(uint32_t first_cluster, uint32_t index)
{
    uint32_t cluster = first_cluster;

    while (index != 0u && cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        cluster = fat32_next_cluster(cluster);
        --index;
    }

    return cluster;
}

static Fat32Result ensure_cluster_chain_length(uint32_t* first_cluster, uint32_t current_count, uint32_t required_count)
{
    Fat32Result result;
    uint32_t i;

    if (required_count <= current_count)
        return FAT32_OK;

    if (current_count == 0u) {
        result = allocate_cluster_chain(required_count, first_cluster);
        return result;
    }

    for (i = current_count; i < required_count; ++i) {
        uint32_t new_cluster;

        result = append_cluster_to_chain(*first_cluster, &new_cluster);
        if (result != FAT32_OK)
            return result;
        result = zero_cluster(new_cluster);
        if (result != FAT32_OK) {
            fat32_set_cluster(new_cluster, 0u);
            return result;
        }
    }

    return FAT32_OK;
}

static Fat32Result write_entry_at(const EntryRef* ref, const DirectoryEntry* entry)
{
    return write_directory_entry(ref->directory_first_cluster, ref->entry_index, entry);
}

static Fat32Result write_entry_span_at(uint32_t first_cluster, uint32_t start_index, const DirectoryEntry* entries, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        Fat32Result result = write_directory_entry(first_cluster, start_index + i, &entries[i]);

        if (result != FAT32_OK)
            return result;
    }

    return FAT32_OK;
}

static Fat32Result mark_entry_deleted(const EntryRef* ref)
{
    DirectoryEntry deleted_entry;
    uint32_t i;

    for (i = 0; i < ref->lfn_entry_count + 1u; ++i) {
        Fat32Result result = read_directory_entry(ref->directory_first_cluster, ref->entry_index - ref->lfn_entry_count + i, &deleted_entry);

        if (result != FAT32_OK)
            return result;
        deleted_entry.name[0] = FAT32_DELETED;
        result = write_directory_entry(ref->directory_first_cluster, ref->entry_index - ref->lfn_entry_count + i, &deleted_entry);
        if (result != FAT32_OK)
            return result;
    }

    return FAT32_OK;
}

static Fat32Result mark_entry_range_deleted(uint32_t directory_first_cluster, uint32_t start_index, uint32_t count)
{
    DirectoryEntry deleted_entry;
    uint32_t i;

    for (i = 0; i < count; ++i) {
        Fat32Result result = read_directory_entry(directory_first_cluster, start_index + i, &deleted_entry);

        if (result != FAT32_OK)
            return result;
        deleted_entry.name[0] = FAT32_DELETED;
        result = write_directory_entry(directory_first_cluster, start_index + i, &deleted_entry);
        if (result != FAT32_OK)
            return result;
    }

    return FAT32_OK;
}

static Fat32Result find_free_entry_in_cluster_dir(uint32_t first_cluster, uint32_t needed_entries, EntryRef* out)
{
    uint32_t cluster = first_cluster;
    uint32_t entry_base_index = 0u;
    uint32_t run_start = 0u;
    uint32_t run_count = 0u;

    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return FAT32_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = entries_per_cluster();
        for (i = 0; i < entry_count; ++i) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == FAT32_DELETED) {
                if (run_count == 0u)
                    run_start = entry_base_index + i;
                ++run_count;
                if (run_count >= needed_entries) {
                    out->found = false;
                    out->directory_first_cluster = first_cluster;
                    out->entry_index = run_start;
                    out->lfn_entry_count = needed_entries > 0u ? needed_entries - 1u : 0u;
                    return FAT32_OK;
                }
            } else {
                run_count = 0u;
            }
        }

        entry_base_index += entry_count;
        if (fat32_next_cluster(cluster) >= FAT32_END_OF_CHAIN)
            break;
        cluster = fat32_next_cluster(cluster);
    }

    {
        uint32_t new_cluster = 0u;
        Fat32Result result = append_cluster_to_chain(first_cluster, &new_cluster);

        if (result != FAT32_OK)
            return result;
        result = zero_cluster(new_cluster);
        if (result != FAT32_OK)
            return result;
        if (!flush_fat())
            return FAT32_IO_ERROR;

        out->found = false;
        out->directory_first_cluster = first_cluster;
        out->entry_index = run_count != 0u ? run_start : entry_base_index;
        out->lfn_entry_count = needed_entries > 0u ? needed_entries - 1u : 0u;
        return FAT32_OK;
    }
}

static Fat32Result find_free_entry_in_directory(const DirectoryRef* directory, uint32_t needed_entries, EntryRef* out)
{
    return find_free_entry_in_cluster_dir(directory->first_cluster, needed_entries, out);
}

static bool directory_entry_is_special(const DirectoryEntry* entry)
{
    return entry->name[0] == '.';
}

static bool directory_is_empty(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    LfnState lfn;

    lfn_state_reset(&lfn);
    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t entry_count;
        uint32_t i;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return false;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = entries_per_cluster();
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];

            if (entry->name[0] == 0x00)
                return true;
            if (entry->name[0] == FAT32_DELETED) {
                lfn_state_reset(&lfn);
                continue;
            }
            if (entry->attributes == FAT32_LFN) {
                process_lfn_entry(entry, &lfn);
                continue;
            }
            if (should_skip_entry(entry)) {
                lfn_state_reset(&lfn);
                continue;
            }
            if (directory_entry_is_special(entry))
                continue;
            return false;
        }

        cluster = fat32_next_cluster(cluster);
    }

    return true;
}

static Fat32Result update_directory_parent(uint32_t dir_cluster, uint32_t parent_cluster)
{
    DirectoryEntry* entries;

    if (!read_cluster(dir_cluster))
        return FAT32_IO_ERROR;

    entries = (DirectoryEntry*)g_cluster_buffer;
    entry_set_first_cluster(&entries[1], parent_cluster);
    if (!write_cluster(dir_cluster))
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

static Fat32Result write_data_to_chain(uint32_t first_cluster, uint32_t start_offset, const uint8_t* data, uint32_t size)
{
    uint32_t offset = start_offset;
    uint32_t remaining = size;
    const uint8_t* cursor = data;
    uint32_t cluster;

    if (size == 0u)
        return FAT32_OK;
    if (first_cluster < 2u)
        return FAT32_IO_ERROR;

    cluster = get_cluster_at_index(first_cluster, offset / g_cluster_size);
    offset %= g_cluster_size;
    if (cluster < 2u || cluster >= FAT32_END_OF_CHAIN)
        return FAT32_IO_ERROR;

    while (remaining != 0u) {
        uint32_t chunk;

        if (!read_cluster(cluster))
            return FAT32_IO_ERROR;

        chunk = g_cluster_size - offset;
        if (chunk > remaining)
            chunk = remaining;
        k_memcpy(g_cluster_buffer + offset, cursor, chunk);
        if (!write_cluster(cluster))
            return FAT32_IO_ERROR;

        remaining -= chunk;
        cursor += chunk;
        offset = 0u;
        if (remaining == 0u)
            break;

        cluster = fat32_next_cluster(cluster);
        if (cluster < 2u || cluster >= FAT32_END_OF_CHAIN)
            return FAT32_IO_ERROR;
    }

    return FAT32_OK;
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

static Fat32Result lookup_parent_and_name(const char* path, DirectoryRef* parent_dir, char* name_out, char* parent_path_out)
{
    Fat32Result result = split_path(path, parent_path_out, name_out);

    if (result != FAT32_OK)
        return result;
    if (!validate_long_name(name_out, NULL))
        return FAT32_INVALID_NAME;
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

bool fat32_mount(uint32_t partition_lba_start, uint16_t bytes_per_sector)
{
    const BootSector* boot_sector;
    uint32_t fat_bytes;
    uint32_t fat_entries;
    uint32_t total_sectors;
    uint32_t fat_area_sectors;
    uint32_t non_data_sectors;
    uint32_t data_sectors;
    uint32_t total_clusters;

    if (bytes_per_sector != FAT32_SECTOR_SIZE)
        return false;
    if (!read_sector(partition_lba_start, g_sector_buffer))
        return false;
    boot_sector = (const BootSector*)g_sector_buffer;
    if (boot_sector->bytes_per_sector != FAT32_SECTOR_SIZE)
        return false;
    if (boot_sector->sectors_per_fat32 == 0 || boot_sector->sectors_per_fat32 > FAT32_MAX_FAT_SECTORS)
        return false;
    if (boot_sector->sectors_per_cluster == 0 || boot_sector->sectors_per_cluster > FAT32_MAX_CLUSTER_SECTORS)
        return false;
    if (boot_sector->fat_count == 0 || boot_sector->reserved_sectors == 0u)
        return false;
    if (boot_sector->root_cluster < 2u)
        return false;

    total_sectors = boot_sector->total_sectors16 != 0 ? boot_sector->total_sectors16 : boot_sector->total_sectors32;
    if (total_sectors == 0u)
        return false;
    fat_area_sectors = (uint32_t)boot_sector->fat_count * boot_sector->sectors_per_fat32;
    non_data_sectors = (uint32_t)boot_sector->reserved_sectors + fat_area_sectors;
    if (non_data_sectors >= total_sectors)
        return false;

    data_sectors = total_sectors - non_data_sectors;
    total_clusters = data_sectors / boot_sector->sectors_per_cluster + 2u;

    fat_bytes = (uint32_t)boot_sector->sectors_per_fat32 * FAT32_SECTOR_SIZE;
    fat_entries = fat_bytes / sizeof(g_fat[0]);
    if (total_clusters > fat_entries)
        return false;
    if (boot_sector->root_cluster >= total_clusters)
        return false;

    g_partition_lba = partition_lba_start;
    g_bytes_per_sector = boot_sector->bytes_per_sector;
    g_sectors_per_cluster = boot_sector->sectors_per_cluster;
    g_fat_count = boot_sector->fat_count;
    g_reserved_sectors = boot_sector->reserved_sectors;
    g_sectors_per_fat = boot_sector->sectors_per_fat32;
    g_root_cluster = boot_sector->root_cluster;
    g_fat_lba = g_partition_lba + g_reserved_sectors;
    g_data_lba = g_fat_lba + (uint32_t)g_fat_count * g_sectors_per_fat;
    g_cluster_size = (uint32_t)g_sectors_per_cluster * g_bytes_per_sector;
    g_total_clusters = total_clusters;

    fat_bytes = (uint32_t)g_sectors_per_fat * g_bytes_per_sector;
    if (!read_sectors(g_fat_lba, g_sectors_per_fat, g_fat))
        return false;

    k_memset(g_sector_buffer, 0, sizeof(g_sector_buffer));
    k_memset(g_cluster_buffer, 0, sizeof(g_cluster_buffer));
    k_memset(((uint8_t*)g_fat) + fat_bytes, 0, sizeof(g_fat) - fat_bytes);
    g_mounted = true;
    return true;
}

Fat32Result fat32_stat(const char* path, Fat32NodeInfo* out)
{
    ResolvedPath resolved;
    Fat32Result result;

    if (!g_mounted || path == NULL || out == NULL)
        return FAT32_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT32_OK)
        return result;

    if (resolved.is_root) {
        out->name[0] = '/';
        out->name[1] = '\0';
        out->is_dir = true;
        out->size = 0;
    } else {
        const char* last = path;
        const char* cursor = path;

        while (*cursor != '\0') {
            if (*cursor == '/' && cursor[1] != '\0')
                last = cursor + 1;
            ++cursor;
        }
        *out = decode_node_info(&resolved.entry, last);
    }

    return FAT32_OK;
}

static Fat32Result list_cluster_dir(uint32_t first_cluster, Fat32ListCallback callback, void* context)
{
    uint32_t cluster = first_cluster;
    LfnState lfn;

    lfn_state_reset(&lfn);
    while (cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t i;
        uint32_t entry_count;
        DirectoryEntry* entries;

        if (!read_cluster(cluster))
            return FAT32_IO_ERROR;

        entries = (DirectoryEntry*)g_cluster_buffer;
        entry_count = entries_per_cluster();
        for (i = 0; i < entry_count; ++i) {
            DirectoryEntry* entry = &entries[i];
            Fat32NodeInfo public_entry;
            char entry_name[FAT32_NAME_CAPACITY];

            if (entry->name[0] == 0x00)
                return FAT32_OK;
            if (entry->name[0] == FAT32_DELETED) {
                lfn_state_reset(&lfn);
                continue;
            }
            if (entry->attributes == FAT32_LFN) {
                process_lfn_entry(entry, &lfn);
                continue;
            }
            if (should_skip_entry(entry)) {
                lfn_state_reset(&lfn);
                continue;
            }
            if (entry->name[0] == '.')
                continue;

            decode_directory_entry_name(entry, &lfn, entry_name);
            public_entry = decode_node_info(entry, entry_name);
            lfn_state_reset(&lfn);
            if (!callback(&public_entry, context))
                return FAT32_OK;
        }

        cluster = fat32_next_cluster(cluster);
    }

    return FAT32_OK;
}

Fat32Result fat32_list_dir(const char* path, Fat32ListCallback callback, void* context)
{
    ResolvedPath resolved;
    Fat32Result result;

    if (!g_mounted || path == NULL || callback == NULL)
        return FAT32_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT32_OK)
        return result;
    if (resolved.is_root)
        return list_cluster_dir(g_root_cluster, callback, context);
    if ((resolved.entry.attributes & FAT32_DIRECTORY) == 0)
        return FAT32_NOT_A_DIRECTORY;

    return list_cluster_dir(entry_first_cluster(&resolved.entry), callback, context);
}

Fat32Result fat32_read_file(const char* path, Fat32ReadCallback callback, void* context)
{
    ResolvedPath resolved;
    Fat32Result result;
    uint32_t remaining;
    uint32_t cluster;

    if (!g_mounted || path == NULL || callback == NULL)
        return FAT32_IO_ERROR;

    result = resolve_path(path, &resolved);
    if (result != FAT32_OK)
        return result;
    if (resolved.is_root)
        return FAT32_IS_A_DIRECTORY;
    if ((resolved.entry.attributes & FAT32_DIRECTORY) != 0)
        return FAT32_IS_A_DIRECTORY;

    remaining = resolved.entry.size;
    cluster = entry_first_cluster(&resolved.entry);

    while (remaining > 0 && cluster >= 2u && cluster < FAT32_END_OF_CHAIN) {
        uint32_t chunk = g_cluster_size;

        if (!read_cluster(cluster))
            return FAT32_IO_ERROR;
        if (chunk > remaining)
            chunk = remaining;
        if (!callback(g_cluster_buffer, chunk, context))
            return FAT32_IO_ERROR;

        remaining -= chunk;
        cluster = fat32_next_cluster(cluster);
    }

    return remaining == 0u ? FAT32_OK : FAT32_IO_ERROR;
}

Fat32Result fat32_write_file(const char* path, const uint8_t* data, uint32_t size, bool append)
{
    DirectoryRef parent_dir;
    char name[FAT32_NAME_CAPACITY];
    char parent_path[128];
    EntryRef entry_ref;
    EntryRef slot;
    DirectoryEntry new_entries[FAT32_MAX_NAME_ENTRIES];
    DirectoryEntry entry;
    Fat32Result result;
    uint32_t old_size = 0u;
    uint32_t first_cluster = 0u;
    uint32_t current_clusters = 0u;
    uint32_t required_clusters = 0u;
    uint32_t entry_count = 0u;
    bool entry_exists = false;

    if (!g_mounted || path == NULL)
        return FAT32_IO_ERROR;
    if (size != 0u && data == NULL)
        return FAT32_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, name, parent_path);
    if (result != FAT32_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, name, &entry_ref);
    if (result == FAT32_OK) {
        entry = entry_ref.entry;
        if ((entry.attributes & FAT32_DIRECTORY) != 0)
            return FAT32_IS_A_DIRECTORY;
        old_size = entry.size;
        current_clusters = clusters_for_size(old_size);
        first_cluster = entry_first_cluster(&entry);
        entry_exists = true;
    } else if (result == FAT32_NOT_FOUND) {
        k_memset(&entry, 0, sizeof(entry));
        entry.attributes = FAT32_ARCHIVE;
        first_cluster = 0u;
    } else {
        return result;
    }

    if (!append) {
        required_clusters = clusters_for_size(size);
        result = ensure_cluster_chain_length(&first_cluster, current_clusters, required_clusters);
        if (result != FAT32_OK)
            return result;
        result = trim_cluster_chain(&first_cluster, required_clusters);
        if (result != FAT32_OK)
            return result;
        if (!flush_fat())
            return FAT32_IO_ERROR;

        result = write_data_to_chain(first_cluster, 0u, data, size);
        if (result != FAT32_OK)
            return result;

        entry_set_first_cluster(&entry, first_cluster);
        entry.size = size;
        if (entry_exists)
            return write_entry_at(&entry_ref, &entry);

        result = build_name_entries(parent_dir.first_cluster, name, FAT32_ARCHIVE, first_cluster, size, NULL, new_entries, &entry_count);
        if (result != FAT32_OK)
            return result;
        result = find_free_entry_in_directory(&parent_dir, entry_count, &slot);
        if (result != FAT32_OK)
            return result;
        return write_entry_span_at(parent_dir.first_cluster, slot.entry_index, new_entries, entry_count);
    }

    required_clusters = clusters_for_size(old_size + size);
    result = ensure_cluster_chain_length(&first_cluster, current_clusters, required_clusters);
    if (result != FAT32_OK)
        return result;
    if (!flush_fat())
        return FAT32_IO_ERROR;
    result = write_data_to_chain(first_cluster, old_size, data, size);
    if (result != FAT32_OK)
        return result;

    entry_set_first_cluster(&entry, first_cluster);
    entry.size = old_size + size;
    if (entry_exists)
        return write_entry_at(&entry_ref, &entry);

    result = build_name_entries(parent_dir.first_cluster, name, FAT32_ARCHIVE, first_cluster, old_size + size, NULL, new_entries, &entry_count);
    if (result != FAT32_OK)
        return result;
    result = find_free_entry_in_directory(&parent_dir, entry_count, &slot);
    if (result != FAT32_OK)
        return result;
    return write_entry_span_at(parent_dir.first_cluster, slot.entry_index, new_entries, entry_count);
}

Fat32Result fat32_make_dir(const char* path)
{
    DirectoryRef parent_dir;
    char name[FAT32_NAME_CAPACITY];
    char parent_path[128];
    EntryRef existing;
    EntryRef slot;
    DirectoryEntry name_entries[FAT32_MAX_NAME_ENTRIES];
    uint32_t dir_cluster = 0u;
    DirectoryEntry* entries;
    Fat32Result result;
    uint32_t entry_count = 0u;

    if (!g_mounted || path == NULL)
        return FAT32_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, name, parent_path);
    if (result != FAT32_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, name, &existing);
    if (result == FAT32_OK)
        return FAT32_ALREADY_EXISTS;
    if (result != FAT32_NOT_FOUND)
        return result;

    result = allocate_cluster_chain(1u, &dir_cluster);
    if (result != FAT32_OK)
        return result;
    if (!flush_fat())
        return FAT32_IO_ERROR;

    k_memset(g_cluster_buffer, 0, g_cluster_size);
    entries = (DirectoryEntry*)g_cluster_buffer;
    k_memset(&entries[0], 0, sizeof(DirectoryEntry));
    fill_special_dir_name(entries[0].name, ".");
    entries[0].attributes = FAT32_DIRECTORY;
    entry_set_first_cluster(&entries[0], dir_cluster);
    k_memset(&entries[1], 0, sizeof(DirectoryEntry));
    fill_special_dir_name(entries[1].name, "..");
    entries[1].attributes = FAT32_DIRECTORY;
    entry_set_first_cluster(&entries[1], parent_dir.is_root ? 0u : parent_dir.first_cluster);
    if (!write_cluster(dir_cluster))
        return FAT32_IO_ERROR;

    result = build_name_entries(parent_dir.first_cluster, name, FAT32_DIRECTORY, dir_cluster, 0u, NULL, name_entries, &entry_count);
    if (result != FAT32_OK)
        return result;
    result = find_free_entry_in_directory(&parent_dir, entry_count, &slot);
    if (result != FAT32_OK)
        return result;
    return write_entry_span_at(parent_dir.first_cluster, slot.entry_index, name_entries, entry_count);
}

Fat32Result fat32_remove(const char* path)
{
    DirectoryRef parent_dir;
    char name[FAT32_NAME_CAPACITY];
    char parent_path[128];
    EntryRef entry_ref;
    Fat32Result result;

    if (!g_mounted || path == NULL)
        return FAT32_IO_ERROR;

    result = lookup_parent_and_name(path, &parent_dir, name, parent_path);
    if (result != FAT32_OK)
        return result;

    result = find_entry_in_directory(&parent_dir, name, &entry_ref);
    if (result != FAT32_OK)
        return result;

    if ((entry_ref.entry.attributes & FAT32_DIRECTORY) != 0) {
        if (!directory_is_empty(entry_first_cluster(&entry_ref.entry)))
            return FAT32_DIRECTORY_NOT_EMPTY;
    }

    result = mark_entry_deleted(&entry_ref);
    if (result != FAT32_OK)
        return result;
    if (entry_first_cluster(&entry_ref.entry) >= 2u)
        free_cluster_chain_in_memory(entry_first_cluster(&entry_ref.entry));
    if (!flush_fat())
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

Fat32Result fat32_rename(const char* old_path, const char* new_path)
{
    DirectoryRef old_parent_dir;
    DirectoryRef new_parent_dir;
    char old_name[FAT32_NAME_CAPACITY];
    char new_name[FAT32_NAME_CAPACITY];
    char old_parent_path[128];
    char new_parent_path[128];
    EntryRef old_ref;
    EntryRef existing_ref;
    EntryRef new_slot;
    DirectoryEntry new_entries[FAT32_MAX_NAME_ENTRIES];
    Fat32Result result;
    uint32_t new_entry_count = 0u;
    uint32_t old_span_start;
    uint32_t old_span_count;
    bool same_parent;

    if (!g_mounted || old_path == NULL || new_path == NULL)
        return FAT32_IO_ERROR;
    if (k_strcmp(old_path, new_path) == 0)
        return FAT32_OK;

    result = lookup_parent_and_name(old_path, &old_parent_dir, old_name, old_parent_path);
    if (result != FAT32_OK)
        return result;
    result = lookup_parent_and_name(new_path, &new_parent_dir, new_name, new_parent_path);
    if (result != FAT32_OK)
        return result;

    result = find_entry_in_directory(&old_parent_dir, old_name, &old_ref);
    if (result != FAT32_OK)
        return result;

    if ((old_ref.entry.attributes & FAT32_DIRECTORY) != 0 && path_is_descendant_of(new_parent_path, old_path))
        return FAT32_INVALID_NAME;

    result = find_entry_in_directory(&new_parent_dir, new_name, &existing_ref);
    if (result == FAT32_OK)
        return FAT32_ALREADY_EXISTS;
    if (result != FAT32_NOT_FOUND)
        return result;

    same_parent = old_parent_dir.is_root == new_parent_dir.is_root && old_parent_dir.first_cluster == new_parent_dir.first_cluster;
    result = build_name_entries(new_parent_dir.first_cluster, new_name, old_ref.entry.attributes, entry_first_cluster(&old_ref.entry), old_ref.entry.size, same_parent ? old_ref.entry.name : NULL, new_entries, &new_entry_count);
    if (result != FAT32_OK)
        return result;

    old_span_start = old_ref.entry_index - old_ref.lfn_entry_count;
    old_span_count = old_ref.lfn_entry_count + 1u;

    if (same_parent && new_entry_count <= old_span_count) {
        result = write_entry_span_at(old_parent_dir.first_cluster, old_span_start, new_entries, new_entry_count);
        if (result != FAT32_OK)
            return result;
        if (new_entry_count < old_span_count) {
            result = mark_entry_range_deleted(old_parent_dir.first_cluster, old_span_start + new_entry_count, old_span_count - new_entry_count);
            if (result != FAT32_OK)
                return result;
        }
    } else {
        result = find_free_entry_in_directory(&new_parent_dir, new_entry_count, &new_slot);
        if (result != FAT32_OK)
            return result;
        result = write_entry_span_at(new_parent_dir.first_cluster, new_slot.entry_index, new_entries, new_entry_count);
        if (result != FAT32_OK)
            return result;
        result = mark_entry_deleted(&old_ref);
        if (result != FAT32_OK)
            return result;
    }

    if ((old_ref.entry.attributes & FAT32_DIRECTORY) != 0)
        return update_directory_parent(entry_first_cluster(&old_ref.entry), new_parent_dir.is_root ? 0u : new_parent_dir.first_cluster);

    return FAT32_OK;
}
