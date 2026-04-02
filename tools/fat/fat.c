#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t bool;

#define true 1
#define false 0

#define FAT_DIRECTORY 0x10u
#define FAT_LFN 0x0Fu
#define FAT16_END_OF_CHAIN 0xFFF8u

typedef struct __attribute__((packed)) PartitionEntry {
    uint8_t status;
    uint8_t first_chs[3];
    uint8_t type;
    uint8_t last_chs[3];
    uint32_t first_lba;
    uint32_t sector_count;
} PartitionEntry;

typedef struct __attribute__((packed)) BootSector {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t dir_entry_count;
    uint16_t total_sectors;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t large_sector_count;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t signature;
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

static BootSector g_boot_sector;
static PartitionEntry g_partition;
static uint16_t* g_fat;
static uint32_t g_root_lba;
static uint32_t g_root_sectors;
static uint32_t g_data_lba;

static bool read_sectors(FILE* disk, uint32_t lba, uint32_t count, void* buffer_out)
{
    bool ok = true;
    ok = ok && (fseek(disk, (long)lba * (long)g_boot_sector.bytes_per_sector, SEEK_SET) == 0);
    ok = ok && (fread(buffer_out, g_boot_sector.bytes_per_sector, count, disk) == count);
    return ok;
}

static uint32_t partition_lba(uint32_t relative_lba)
{
    return g_partition.first_lba + relative_lba;
}

static uint16_t fat16_next_cluster(uint16_t cluster)
{
    return g_fat[cluster];
}

static uint32_t cluster_to_lba(uint16_t cluster)
{
    return g_data_lba + (uint32_t)(cluster - 2u) * g_boot_sector.sectors_per_cluster;
}

static bool init_image(FILE* disk)
{
    PartitionEntry partitions[4];
    uint32_t root_size;
    uint32_t total_sectors;
    size_t partition_index;
    bool found_active = false;

    if (fseek(disk, 0, SEEK_SET) != 0)
        return false;

    if (fseek(disk, 0x1BE, SEEK_SET) != 0)
        return false;

    if (fread(partitions, sizeof(PartitionEntry), 4, disk) != 4)
        return false;

    for (partition_index = 0; partition_index < 4; ++partition_index) {
        if (partitions[partition_index].status == 0x80) {
            g_partition = partitions[partition_index];
            found_active = true;
            break;
        }
    }

    if (!found_active)
        return false;

    if (g_partition.first_lba == 0 || g_partition.sector_count == 0)
        return false;

    if (fseek(disk, (long)g_partition.first_lba * 512L, SEEK_SET) != 0)
        return false;

    if (fread(&g_boot_sector, sizeof(g_boot_sector), 1, disk) != 1)
        return false;

    total_sectors = g_boot_sector.total_sectors ? g_boot_sector.total_sectors : g_boot_sector.large_sector_count;
    if (total_sectors == 0 || g_boot_sector.sectors_per_fat == 0)
        return false;

    g_root_lba = partition_lba(g_boot_sector.reserved_sectors + (uint32_t)g_boot_sector.fat_count * g_boot_sector.sectors_per_fat);
    root_size = (uint32_t)sizeof(DirectoryEntry) * g_boot_sector.dir_entry_count;
    g_root_sectors = root_size / g_boot_sector.bytes_per_sector;
    if ((root_size % g_boot_sector.bytes_per_sector) != 0)
        ++g_root_sectors;
    g_data_lba = g_root_lba + g_root_sectors;

    g_fat = (uint16_t*)malloc((size_t)g_boot_sector.sectors_per_fat * g_boot_sector.bytes_per_sector);
    if (!g_fat)
        return false;

    return read_sectors(disk, partition_lba(g_boot_sector.reserved_sectors), g_boot_sector.sectors_per_fat, g_fat);
}

static void print_name(const uint8_t name[11])
{
    bool has_extension = false;
    int i;

    for (i = 8; i < 11; ++i) {
        if (name[i] != ' ') {
            has_extension = true;
            break;
        }
    }

    for (i = 0; i < 8 && name[i] != ' '; ++i)
        putchar(name[i]);

    if (has_extension) {
        putchar('.');
        for (i = 8; i < 11 && name[i] != ' '; ++i)
            putchar(name[i]);
    }
}

static void make_fat_name(const char* name, uint8_t out[11])
{
    const char* dot = strchr(name, '.');
    int i;

    memset(out, ' ', 11);
    if (!dot)
        dot = name + strlen(name);

    for (i = 0; i < 8 && name[i] != '\0' && name + i < dot; ++i)
        out[i] = (uint8_t)toupper((unsigned char)name[i]);

    if (*dot == '.') {
        for (i = 0; i < 3 && dot[i + 1] != '\0'; ++i)
            out[i + 8] = (uint8_t)toupper((unsigned char)dot[i + 1]);
    }
}

static bool is_printable_entry(const DirectoryEntry* entry)
{
    if (entry->name[0] == 0x00 || entry->name[0] == 0xE5)
        return false;
    if (entry->attributes == FAT_LFN)
        return false;
    return true;
}

static bool read_cluster_chain(FILE* disk, uint16_t first_cluster, uint8_t** data_out, uint32_t* size_out)
{
    uint16_t cluster = first_cluster;
    uint32_t cluster_size = (uint32_t)g_boot_sector.sectors_per_cluster * g_boot_sector.bytes_per_sector;
    uint8_t* buffer = NULL;
    uint32_t capacity = 0;
    uint32_t used = 0;

    if (cluster < 2u)
        return false;

    while (cluster < FAT16_END_OF_CHAIN) {
        uint8_t* next_buffer = (uint8_t*)realloc(buffer, capacity + cluster_size);
        if (!next_buffer) {
            free(buffer);
            return false;
        }

        buffer = next_buffer;
        if (!read_sectors(disk, cluster_to_lba(cluster), g_boot_sector.sectors_per_cluster, buffer + capacity)) {
            free(buffer);
            return false;
        }

        capacity += cluster_size;
        used += cluster_size;
        cluster = fat16_next_cluster(cluster);
    }

    *data_out = buffer;
    *size_out = used;
    return true;
}

static bool read_directory(FILE* disk, const DirectoryEntry* dir, DirectoryEntry** entries_out, uint32_t* count_out)
{
    uint8_t* raw = NULL;
    uint32_t bytes = 0;

    if (!dir) {
        bytes = g_root_sectors * g_boot_sector.bytes_per_sector;
        raw = (uint8_t*)malloc(bytes);
        if (!raw)
            return false;
        if (!read_sectors(disk, g_root_lba, g_root_sectors, raw)) {
            free(raw);
            return false;
        }
    } else {
        if (!read_cluster_chain(disk, dir->first_cluster_low, &raw, &bytes))
            return false;
    }

    *entries_out = (DirectoryEntry*)raw;
    *count_out = bytes / sizeof(DirectoryEntry);
    return true;
}

static DirectoryEntry* find_entry(DirectoryEntry* entries, uint32_t count, const char* name)
{
    uint8_t fat_name[11];
    uint32_t i;

    make_fat_name(name, fat_name);

    for (i = 0; i < count; ++i) {
        if (!is_printable_entry(&entries[i])) {
            if (entries[i].name[0] == 0x00)
                break;
            continue;
        }

        if (memcmp(entries[i].name, fat_name, 11) == 0)
            return &entries[i];
    }

    return NULL;
}

static DirectoryEntry resolve_path(FILE* disk, const char* path, bool* ok_out)
{
    DirectoryEntry current = {0};
    DirectoryEntry* entries = NULL;
    DirectoryEntry* found = NULL;
    uint32_t count = 0;
    char component[64];
    const char* cursor = path;
    bool have_current = false;

    *ok_out = false;
    while (*cursor == '/')
        ++cursor;

    if (*cursor == '\0') {
        *ok_out = true;
        return current;
    }

    while (*cursor != '\0') {
        const char* slash = strchr(cursor, '/');
        size_t len = slash ? (size_t)(slash - cursor) : strlen(cursor);

        if (len == 0 || len >= sizeof(component))
            return current;

        memcpy(component, cursor, len);
        component[len] = '\0';

        if (!read_directory(disk, have_current ? &current : NULL, &entries, &count))
            return current;

        found = find_entry(entries, count, component);
        if (!found) {
            free(entries);
            return current;
        }

        current = *found;
        have_current = true;
        free(entries);

        if (!slash)
            break;

        if ((current.attributes & FAT_DIRECTORY) == 0)
            return current;

        cursor = slash + 1;
    }

    *ok_out = true;
    return current;
}

static bool dump_file(FILE* disk, const DirectoryEntry* file)
{
    uint32_t remaining = file->size;
    uint16_t cluster = file->first_cluster_low;
    uint32_t cluster_size = (uint32_t)g_boot_sector.sectors_per_cluster * g_boot_sector.bytes_per_sector;
    uint8_t* buffer = (uint8_t*)malloc(cluster_size);

    if (!buffer)
        return false;

    while (cluster < FAT16_END_OF_CHAIN && remaining > 0) {
        uint32_t chunk = cluster_size;
        if (!read_sectors(disk, cluster_to_lba(cluster), g_boot_sector.sectors_per_cluster, buffer)) {
            free(buffer);
            return false;
        }

        if (remaining < chunk)
            chunk = remaining;

        for (uint32_t i = 0; i < chunk; ++i) {
            if (isprint(buffer[i]) || buffer[i] == '\n' || buffer[i] == '\r' || buffer[i] == '\t')
                fputc(buffer[i], stdout);
            else
                printf("<%02x>", buffer[i]);
        }

        remaining -= chunk;
        cluster = fat16_next_cluster(cluster);
    }

    free(buffer);
    return true;
}

static bool list_root(FILE* disk)
{
    DirectoryEntry* entries = NULL;
    uint32_t count = 0;

    if (!read_directory(disk, NULL, &entries, &count))
        return false;

    for (uint32_t i = 0; i < count; ++i) {
        if (!is_printable_entry(&entries[i])) {
            if (entries[i].name[0] == 0x00)
                break;
            continue;
        }

        print_name(entries[i].name);
        if (entries[i].attributes & FAT_DIRECTORY)
            printf(" <DIR>");
        printf("\n");
    }

    free(entries);
    return true;
}

int main(int argc, char** argv)
{
    FILE* disk;
    DirectoryEntry entry;
    bool ok = false;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <disk image> <path|/>\n", argv[0]);
        return 1;
    }

    disk = fopen(argv[1], "rb");
    if (!disk) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    if (!init_image(disk)) {
        fprintf(stderr, "failed to initialize FAT16 image\n");
        fclose(disk);
        return 1;
    }

    if (strcmp(argv[2], "/") == 0) {
        if (!list_root(disk)) {
            fprintf(stderr, "failed to list root directory\n");
            fclose(disk);
            free(g_fat);
            return 1;
        }
    } else {
        entry = resolve_path(disk, argv[2], &ok);
        if (!ok) {
            fprintf(stderr, "%s not found\n", argv[2]);
            fclose(disk);
            free(g_fat);
            return 1;
        }

        if (entry.attributes & FAT_DIRECTORY) {
            DirectoryEntry* entries = NULL;
            uint32_t count = 0;
            if (!read_directory(disk, &entry, &entries, &count)) {
                fprintf(stderr, "failed to read directory\n");
                fclose(disk);
                free(g_fat);
                return 1;
            }

            for (uint32_t i = 0; i < count; ++i) {
                if (!is_printable_entry(&entries[i])) {
                    if (entries[i].name[0] == 0x00)
                        break;
                    continue;
                }
                print_name(entries[i].name);
                if (entries[i].attributes & FAT_DIRECTORY)
                    printf(" <DIR>");
                printf("\n");
            }

            free(entries);
        } else if (!dump_file(disk, &entry)) {
            fprintf(stderr, "failed to dump file\n");
            fclose(disk);
            free(g_fat);
            return 1;
        }
    }

    fclose(disk);
    free(g_fat);
    return 0;
}
