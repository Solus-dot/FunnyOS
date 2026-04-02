#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t bool;

#define true 1
#define false 0
#define FAT12_DIRECTORY 0x10

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
static uint8_t* g_fat;
static uint32_t g_root_lba;
static uint32_t g_root_sectors;
static uint32_t g_data_lba;

static bool read_sectors(FILE* disk, uint32_t lba, uint32_t count, void* buffer_out)
{
    bool ok = true;
    ok = ok && (fseek(disk, (long)(lba * g_boot_sector.bytes_per_sector), SEEK_SET) == 0);
    ok = ok && (fread(buffer_out, g_boot_sector.bytes_per_sector, count, disk) == count);
    return ok;
}

static bool init_image(FILE* disk)
{
    uint32_t root_size;

    if (fread(&g_boot_sector, sizeof(g_boot_sector), 1, disk) != 1)
        return false;

    g_root_lba = g_boot_sector.reserved_sectors + g_boot_sector.sectors_per_fat * g_boot_sector.fat_count;
    root_size = sizeof(DirectoryEntry) * g_boot_sector.dir_entry_count;
    g_root_sectors = root_size / g_boot_sector.bytes_per_sector;
    if (root_size % g_boot_sector.bytes_per_sector != 0)
        ++g_root_sectors;

    g_data_lba = g_root_lba + g_root_sectors;

    g_fat = (uint8_t*)malloc(g_boot_sector.sectors_per_fat * g_boot_sector.bytes_per_sector);
    if (!g_fat)
        return false;

    return read_sectors(disk, g_boot_sector.reserved_sectors, g_boot_sector.sectors_per_fat, g_fat);
}

static uint16_t fat12_next_cluster(uint16_t cluster)
{
    uint32_t fat_index = (uint32_t)cluster * 3u / 2u;
    uint16_t value = *(uint16_t*)(g_fat + fat_index);
    return (cluster & 1u) ? (uint16_t)(value >> 4) : (uint16_t)(value & 0x0fffu);
}

static uint32_t cluster_to_lba(uint16_t cluster)
{
    return g_data_lba + (uint32_t)(cluster - 2u) * g_boot_sector.sectors_per_cluster;
}

static void print_name(const uint8_t name[11])
{
    bool has_extension = false;

    for (int i = 8; i < 11; ++i) {
        if (name[i] != ' ') {
            has_extension = true;
            break;
        }
    }

    for (int i = 0; i < 8 && name[i] != ' '; ++i)
        putchar(name[i]);

    if (has_extension) {
        putchar('.');
        for (int i = 8; i < 11 && name[i] != ' '; ++i)
            putchar(name[i]);
    }
}

static void make_fat_name(const char* name, uint8_t out[11])
{
    const char* dot = strchr(name, '.');

    memset(out, ' ', 11);
    if (!dot)
        dot = name + strlen(name);

    for (int i = 0; i < 8 && name[i] != '\0' && name + i < dot; ++i)
        out[i] = (uint8_t)toupper((unsigned char)name[i]);

    if (*dot == '.') {
        for (int i = 0; i < 3 && dot[i + 1] != '\0'; ++i)
            out[i + 8] = (uint8_t)toupper((unsigned char)dot[i + 1]);
    }
}

static bool read_directory(FILE* disk, const DirectoryEntry* dir, DirectoryEntry** entries_out, uint32_t* count_out)
{
    uint32_t sectors = g_boot_sector.sectors_per_cluster;
    uint32_t bytes = sectors * g_boot_sector.bytes_per_sector;
    DirectoryEntry* entries;

    if (!dir) {
        bytes = g_root_sectors * g_boot_sector.bytes_per_sector;
        entries = (DirectoryEntry*)malloc(bytes);
        if (!entries)
            return false;
        if (!read_sectors(disk, g_root_lba, g_root_sectors, entries)) {
            free(entries);
            return false;
        }
        *entries_out = entries;
        *count_out = bytes / sizeof(DirectoryEntry);
        return true;
    }

    entries = (DirectoryEntry*)malloc(bytes);
    if (!entries)
        return false;

    if (!read_sectors(disk, cluster_to_lba(dir->first_cluster_low), sectors, entries)) {
        free(entries);
        return false;
    }

    *entries_out = entries;
    *count_out = bytes / sizeof(DirectoryEntry);
    return true;
}

static DirectoryEntry* find_entry(DirectoryEntry* entries, uint32_t count, const char* name)
{
    uint8_t fat_name[11];

    make_fat_name(name, fat_name);

    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].name[0] == 0x00)
            break;
        if (entries[i].name[0] == 0xE5)
            continue;
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

        if ((current.attributes & FAT12_DIRECTORY) == 0)
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
    uint8_t* sector = (uint8_t*)malloc(g_boot_sector.sectors_per_cluster * g_boot_sector.bytes_per_sector);

    if (!sector)
        return false;

    while (cluster < 0x0ff8u && remaining > 0) {
        uint32_t chunk = g_boot_sector.sectors_per_cluster * g_boot_sector.bytes_per_sector;
        if (!read_sectors(disk, cluster_to_lba(cluster), g_boot_sector.sectors_per_cluster, sector)) {
            free(sector);
            return false;
        }

        if (remaining < chunk)
            chunk = remaining;

        for (uint32_t i = 0; i < chunk; ++i) {
            if (isprint(sector[i]) || sector[i] == '\n' || sector[i] == '\r' || sector[i] == '\t')
                fputc(sector[i], stdout);
            else
                printf("<%02x>", sector[i]);
        }

        remaining -= chunk;
        cluster = fat12_next_cluster(cluster);
    }

    free(sector);
    return true;
}

static bool list_root(FILE* disk)
{
    DirectoryEntry* entries = NULL;
    uint32_t count = 0;

    if (!read_directory(disk, NULL, &entries, &count))
        return false;

    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].name[0] == 0x00)
            break;
        if (entries[i].name[0] == 0xE5)
            continue;
        print_name(entries[i].name);
        if (entries[i].attributes & FAT12_DIRECTORY)
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
        fprintf(stderr, "failed to initialize FAT12 image\n");
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
        fclose(disk);
        free(g_fat);
        return 0;
    }

    entry = resolve_path(disk, argv[2], &ok);
    if (!ok) {
        fprintf(stderr, "path not found: %s\n", argv[2]);
        fclose(disk);
        free(g_fat);
        return 1;
    }

    if (entry.attributes & FAT12_DIRECTORY) {
        printf("%s is a directory\n", argv[2]);
        fclose(disk);
        free(g_fat);
        return 0;
    }

    if (!dump_file(disk, &entry)) {
        fprintf(stderr, "failed to read file contents\n");
        fclose(disk);
        free(g_fat);
        return 1;
    }

    printf("\n");
    fclose(disk);
    free(g_fat);
    return 0;
}
