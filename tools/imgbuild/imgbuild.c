#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SIZE (1474560u)
#define SECTOR_SIZE 512u
#define TOTAL_SECTORS 2880u
#define RESERVED_SECTORS 1u
#define FAT_COUNT 2u
#define SECTORS_PER_FAT 9u
#define ROOT_ENTRY_COUNT 224u
#define ROOT_DIR_SECTORS 14u
#define ROOT_DIR_START_SECTOR (RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT)
#define DATA_START_SECTOR (ROOT_DIR_START_SECTOR + ROOT_DIR_SECTORS)
#define CLUSTER_END 0x0ff8u

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

typedef struct FileBlob {
    uint8_t* data;
    uint32_t size;
} FileBlob;

typedef struct Allocation {
    uint16_t first_cluster;
    uint16_t cluster_count;
    uint32_t size;
} Allocation;

static void fail(const char* message)
{
    fprintf(stderr, "imgbuild: %s\n", message);
    exit(1);
}

static FileBlob read_file_blob(const char* path)
{
    FileBlob blob = {0};
    FILE* fp;
    long size;

    if (strcmp(path, "-") == 0)
        return blob;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "imgbuild: cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    if (fseek(fp, 0, SEEK_END) != 0)
        fail("failed to seek file");

    size = ftell(fp);
    if (size < 0)
        fail("failed to read file size");

    if (fseek(fp, 0, SEEK_SET) != 0)
        fail("failed to rewind file");

    blob.size = (uint32_t)size;
    if (blob.size != 0) {
        blob.data = (uint8_t*)malloc(blob.size);
        if (!blob.data)
            fail("out of memory");

        if (fread(blob.data, 1, blob.size, fp) != blob.size)
            fail("failed to read file contents");
    }

    fclose(fp);
    return blob;
}

static void free_blob(FileBlob* blob)
{
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}

static void fat12_set(uint8_t* fat, uint16_t cluster, uint16_t value)
{
    uint32_t index = (uint32_t)cluster * 3u / 2u;

    if ((cluster & 1u) == 0) {
        fat[index] = (uint8_t)(value & 0xffu);
        fat[index + 1] = (uint8_t)((fat[index + 1] & 0xf0u) | ((value >> 8) & 0x0fu));
    } else {
        fat[index] = (uint8_t)((fat[index] & 0x0fu) | ((value << 4) & 0xf0u));
        fat[index + 1] = (uint8_t)((value >> 4) & 0xffu);
    }
}

static uint32_t cluster_offset(uint16_t cluster)
{
    uint32_t sector = DATA_START_SECTOR + (uint32_t)(cluster - 2u);
    return sector * SECTOR_SIZE;
}

static Allocation allocate_blob(uint8_t* image, uint8_t* fat, const FileBlob* blob, uint16_t* next_cluster, uint16_t min_clusters)
{
    Allocation alloc = {0};

    alloc.cluster_count = min_clusters;
    if (blob->size != 0) {
        uint16_t needed = (uint16_t)((blob->size + SECTOR_SIZE - 1u) / SECTOR_SIZE);
        if (needed > alloc.cluster_count)
            alloc.cluster_count = needed;
    }

    alloc.size = blob->size;
    if (alloc.cluster_count == 0)
        return alloc;

    alloc.first_cluster = *next_cluster;

    for (uint16_t i = 0; i < alloc.cluster_count; ++i) {
        uint16_t cluster = (uint16_t)(alloc.first_cluster + i);
        uint16_t next = (i + 1u == alloc.cluster_count) ? 0x0fffu : (uint16_t)(cluster + 1u);

        fat12_set(fat, cluster, next);

        if (cluster_offset(cluster) + SECTOR_SIZE > IMAGE_SIZE)
            fail("image is out of data clusters");

        memset(image + cluster_offset(cluster), 0, SECTOR_SIZE);
    }

    if (blob->size != 0) {
        uint32_t written = 0;
        for (uint16_t i = 0; i < alloc.cluster_count && written < blob->size; ++i) {
            uint32_t offset = cluster_offset((uint16_t)(alloc.first_cluster + i));
            uint32_t remaining = blob->size - written;
            uint32_t chunk = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
            memcpy(image + offset, blob->data + written, chunk);
            written += chunk;
        }
    }

    *next_cluster = (uint16_t)(alloc.first_cluster + alloc.cluster_count);
    return alloc;
}

static void fill_name(uint8_t out[11], const char* raw)
{
    memcpy(out, raw, 11);
}

static void write_dir_entry(DirectoryEntry* entry, const char* raw_name, uint8_t attributes, uint16_t first_cluster, uint32_t size)
{
    memset(entry, 0, sizeof(*entry));
    fill_name(entry->name, raw_name);
    entry->attributes = attributes;
    entry->first_cluster_low = first_cluster;
    entry->size = size;
}

static void write_directory_contents(uint8_t* image, const Allocation* dir_alloc, const Allocation* demo_alloc)
{
    DirectoryEntry* entries = (DirectoryEntry*)(image + cluster_offset(dir_alloc->first_cluster));

    memset(entries, 0, SECTOR_SIZE * dir_alloc->cluster_count);
    write_dir_entry(&entries[0], ".          ", 0x10, dir_alloc->first_cluster, 0);
    write_dir_entry(&entries[1], "..         ", 0x10, 0, 0);

    if (demo_alloc->cluster_count != 0)
        write_dir_entry(&entries[2], "TEST    TXT", 0x20, demo_alloc->first_cluster, demo_alloc->size);
}

int main(int argc, char** argv)
{
    uint8_t* image;
    uint8_t* fat_primary;
    uint8_t* fat_secondary;
    DirectoryEntry* root_entries;
    FileBlob stage1;
    FileBlob stage2;
    FileBlob kernel;
    FileBlob root_test;
    FileBlob demo_test;
    Allocation stage2_alloc;
    Allocation kernel_alloc;
    Allocation root_test_alloc;
    Allocation mydir_alloc;
    Allocation demo_alloc;
    uint16_t next_cluster = 2;
    uint32_t root_index = 0;
    FILE* output;

    if (argc != 7) {
        fprintf(stderr, "usage: %s <image> <stage1> <stage2> <kernel|-> <root_test|-> <demo_test|->\n", argv[0]);
        return 1;
    }

    stage1 = read_file_blob(argv[2]);
    stage2 = read_file_blob(argv[3]);
    kernel = read_file_blob(argv[4]);
    root_test = read_file_blob(argv[5]);
    demo_test = read_file_blob(argv[6]);

    if (stage1.size != SECTOR_SIZE)
        fail("stage1 boot sector must be exactly 512 bytes");
    if (stage2.size == 0)
        fail("stage2 payload is required");

    image = (uint8_t*)calloc(1, IMAGE_SIZE);
    if (!image)
        fail("out of memory");

    memcpy(image, stage1.data, SECTOR_SIZE);

    fat_primary = image + RESERVED_SECTORS * SECTOR_SIZE;
    fat_secondary = fat_primary + SECTORS_PER_FAT * SECTOR_SIZE;
    fat_primary[0] = 0xF0;
    fat_primary[1] = 0xFF;
    fat_primary[2] = 0xFF;

    stage2_alloc = allocate_blob(image, fat_primary, &stage2, &next_cluster, 1);
    kernel_alloc = allocate_blob(image, fat_primary, &kernel, &next_cluster, 0);
    root_test_alloc = allocate_blob(image, fat_primary, &root_test, &next_cluster, 0);
    mydir_alloc = allocate_blob(image, fat_primary, &(FileBlob){0}, &next_cluster, 1);
    demo_alloc = allocate_blob(image, fat_primary, &demo_test, &next_cluster, 0);

    write_directory_contents(image, &mydir_alloc, &demo_alloc);
    memcpy(fat_secondary, fat_primary, SECTORS_PER_FAT * SECTOR_SIZE);

    root_entries = (DirectoryEntry*)(image + ROOT_DIR_START_SECTOR * SECTOR_SIZE);
    write_dir_entry(&root_entries[root_index++], "STAGE2  BIN", 0x20, stage2_alloc.first_cluster, stage2_alloc.size);

    if (kernel_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "KERNEL  BIN", 0x20, kernel_alloc.first_cluster, kernel_alloc.size);

    if (root_test_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "TEST    TXT", 0x20, root_test_alloc.first_cluster, root_test_alloc.size);

    write_dir_entry(&root_entries[root_index], "MYDIR      ", 0x10, mydir_alloc.first_cluster, 0);

    output = fopen(argv[1], "wb");
    if (!output) {
        fprintf(stderr, "imgbuild: cannot create %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    if (fwrite(image, 1, IMAGE_SIZE, output) != IMAGE_SIZE)
        fail("failed to write image");

    fclose(output);

    free(image);
    free_blob(&stage1);
    free_blob(&stage2);
    free_blob(&kernel);
    free_blob(&root_test);
    free_blob(&demo_test);
    return 0;
}
