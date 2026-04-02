#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SIZE_BYTES (64u * 1024u * 1024u)
#define SECTOR_SIZE 512u
#define TOTAL_SECTORS (IMAGE_SIZE_BYTES / SECTOR_SIZE)

#define PARTITION_START_LBA 2048u
#define PARTITION_SECTORS (TOTAL_SECTORS - PARTITION_START_LBA)
#define PARTITION_TYPE_FAT16_LBA 0x0Eu

#define RESERVED_SECTORS 1u
#define FAT_COUNT 2u
#define ROOT_ENTRY_COUNT 512u
#define ROOT_DIR_SECTORS ((ROOT_ENTRY_COUNT * 32u) / SECTOR_SIZE)
#define SECTORS_PER_CLUSTER 4u
#define SECTORS_PER_FAT 126u

#define FAT_START_SECTOR RESERVED_SECTORS
#define ROOT_DIR_START_SECTOR (FAT_START_SECTOR + FAT_COUNT * SECTORS_PER_FAT)
#define DATA_START_SECTOR (ROOT_DIR_START_SECTOR + ROOT_DIR_SECTORS)

#define FAT16_END_OF_CHAIN 0xFFFFu
#define FAT16_CLUSTER_FREE 0x0000u
#define FAT16_DIRECTORY 0x10u
#define FAT16_ARCHIVE 0x20u

#define VBR_STAGE2_LBA_OFFSET 62u
#define VBR_STAGE2_SECTOR_COUNT_OFFSET 66u

typedef struct __attribute__((packed)) PartitionEntry {
    uint8_t status;
    uint8_t first_chs[3];
    uint8_t type;
    uint8_t last_chs[3];
    uint32_t first_lba;
    uint32_t sector_count;
} PartitionEntry;

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
        fail("failed to determine file size");

    if (fseek(fp, 0, SEEK_SET) != 0)
        fail("failed to rewind file");

    blob.size = (uint32_t)size;
    if (blob.size != 0) {
        blob.data = (uint8_t*)malloc(blob.size);
        if (!blob.data)
            fail("out of memory");

        if (fread(blob.data, 1, blob.size, fp) != blob.size)
            fail("failed to read file");
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

static FileBlob make_repeated_text_blob(const char* line, uint32_t repeat_count)
{
    FileBlob blob = {0};
    size_t line_len = strlen(line);
    uint32_t i;

    blob.size = (uint32_t)line_len * repeat_count;
    blob.data = (uint8_t*)malloc(blob.size);
    if (!blob.data)
        fail("out of memory");

    for (i = 0; i < repeat_count; ++i)
        memcpy(blob.data + i * line_len, line, line_len);

    return blob;
}

static uint32_t partition_byte_offset(uint32_t partition_lba)
{
    return (PARTITION_START_LBA + partition_lba) * SECTOR_SIZE;
}

static uint32_t cluster_byte_offset(uint16_t cluster)
{
    uint32_t partition_lba = DATA_START_SECTOR + (uint32_t)(cluster - 2u) * SECTORS_PER_CLUSTER;
    return partition_byte_offset(partition_lba);
}

static uint32_t cluster_to_partition_lba(uint16_t cluster)
{
    return DATA_START_SECTOR + (uint32_t)(cluster - 2u) * SECTORS_PER_CLUSTER;
}

static void fat16_set(uint16_t* fat, uint16_t cluster, uint16_t value)
{
    fat[cluster] = value;
}

static Allocation allocate_blob(uint8_t* image, uint16_t* fat, const FileBlob* blob, uint16_t* next_cluster, uint16_t min_clusters)
{
    Allocation alloc = {0};

    alloc.cluster_count = min_clusters;
    if (blob->size != 0) {
        uint16_t needed = (uint16_t)((blob->size + (SECTOR_SIZE * SECTORS_PER_CLUSTER) - 1u) / (SECTOR_SIZE * SECTORS_PER_CLUSTER));
        if (needed > alloc.cluster_count)
            alloc.cluster_count = needed;
    }

    alloc.size = blob->size;
    if (alloc.cluster_count == 0)
        return alloc;

    alloc.first_cluster = *next_cluster;

    for (uint16_t i = 0; i < alloc.cluster_count; ++i) {
        uint16_t cluster = (uint16_t)(alloc.first_cluster + i);
        uint16_t next = (i + 1u == alloc.cluster_count) ? FAT16_END_OF_CHAIN : (uint16_t)(cluster + 1u);
        uint32_t offset = cluster_byte_offset(cluster);

        if (offset + (SECTOR_SIZE * SECTORS_PER_CLUSTER) > IMAGE_SIZE_BYTES)
            fail("image out of data clusters");

        fat16_set(fat, cluster, next);
        memset(image + offset, 0, SECTOR_SIZE * SECTORS_PER_CLUSTER);
    }

    if (blob->size != 0) {
        uint32_t written = 0;
        for (uint16_t i = 0; i < alloc.cluster_count && written < blob->size; ++i) {
            uint32_t offset = cluster_byte_offset((uint16_t)(alloc.first_cluster + i));
            uint32_t chunk = blob->size - written;
            uint32_t cluster_size = SECTOR_SIZE * SECTORS_PER_CLUSTER;

            if (chunk > cluster_size)
                chunk = cluster_size;

            memcpy(image + offset, blob->data + written, chunk);
            written += chunk;
        }
    }

    *next_cluster = (uint16_t)(alloc.first_cluster + alloc.cluster_count);
    return alloc;
}

static void fill_name(uint8_t out[11], const char* name)
{
    memcpy(out, name, 11);
}

static void write_dir_entry(DirectoryEntry* entry, const char* name, uint8_t attributes, uint16_t first_cluster, uint32_t size)
{
    memset(entry, 0, sizeof(*entry));
    fill_name(entry->name, name);
    entry->attributes = attributes;
    entry->first_cluster_low = first_cluster;
    entry->size = size;
}

static void write_directory_chain(uint8_t* image, const Allocation* dir_alloc, const Allocation* demo_alloc)
{
    DirectoryEntry* entries = (DirectoryEntry*)(image + cluster_byte_offset(dir_alloc->first_cluster));
    uint32_t bytes = (uint32_t)dir_alloc->cluster_count * SECTOR_SIZE * SECTORS_PER_CLUSTER;

    memset(entries, 0, bytes);
    write_dir_entry(&entries[0], ".          ", FAT16_DIRECTORY, dir_alloc->first_cluster, 0);
    write_dir_entry(&entries[1], "..         ", FAT16_DIRECTORY, 0, 0);

    if (demo_alloc->cluster_count != 0)
        write_dir_entry(&entries[2], "TEST    TXT", FAT16_ARCHIVE, demo_alloc->first_cluster, demo_alloc->size);
}

static void write_big_directory_chain(uint8_t* image, const Allocation* dir_alloc)
{
    DirectoryEntry* entries = (DirectoryEntry*)(image + cluster_byte_offset(dir_alloc->first_cluster));
    uint32_t bytes = (uint32_t)dir_alloc->cluster_count * SECTOR_SIZE * SECTORS_PER_CLUSTER;
    uint32_t i;

    memset(entries, 0, bytes);
    write_dir_entry(&entries[0], ".          ", FAT16_DIRECTORY, dir_alloc->first_cluster, 0);
    write_dir_entry(&entries[1], "..         ", FAT16_DIRECTORY, 0, 0);

    for (i = 0; i < 70u; ++i) {
        char name[12];
        snprintf(name, sizeof(name), "ITEM%02u  TXT", i);
        write_dir_entry(&entries[i + 2u], name, FAT16_ARCHIVE, 0, 0);
    }
}

static void write_mbr(uint8_t* image, const FileBlob* mbr_blob)
{
    PartitionEntry* partitions;

    if (mbr_blob->size != SECTOR_SIZE)
        fail("MBR must be exactly 512 bytes");

    memcpy(image, mbr_blob->data, SECTOR_SIZE);
    partitions = (PartitionEntry*)(image + 0x1BE);
    memset(partitions, 0, sizeof(PartitionEntry) * 4u);

    partitions[0].status = 0x80;
    partitions[0].first_chs[0] = 0x01;
    partitions[0].first_chs[1] = 0x01;
    partitions[0].first_chs[2] = 0x00;
    partitions[0].type = PARTITION_TYPE_FAT16_LBA;
    partitions[0].last_chs[0] = 0xFE;
    partitions[0].last_chs[1] = 0xFF;
    partitions[0].last_chs[2] = 0xFF;
    partitions[0].first_lba = PARTITION_START_LBA;
    partitions[0].sector_count = PARTITION_SECTORS;

    image[510] = 0x55;
    image[511] = 0xAA;
}

static void write_vbr_and_metadata(uint8_t* image, const FileBlob* vbr_blob)
{
    uint32_t partition_offset = PARTITION_START_LBA * SECTOR_SIZE;
    uint16_t* fat_primary;
    uint16_t* fat_secondary;

    if (vbr_blob->size != SECTOR_SIZE)
        fail("VBR must be exactly 512 bytes");

    memcpy(image + partition_offset, vbr_blob->data, SECTOR_SIZE);

    fat_primary = (uint16_t*)(image + partition_byte_offset(FAT_START_SECTOR));
    fat_secondary = (uint16_t*)(image + partition_byte_offset(FAT_START_SECTOR + SECTORS_PER_FAT));

    memset(fat_primary, 0, SECTORS_PER_FAT * SECTOR_SIZE);
    fat_primary[0] = 0xFFF8u;
    fat_primary[1] = FAT16_END_OF_CHAIN;
    memcpy(fat_secondary, fat_primary, SECTORS_PER_FAT * SECTOR_SIZE);
}

static void patch_stage2_boot_metadata(uint8_t* image, const Allocation* stage2_alloc)
{
    uint8_t* vbr = image + PARTITION_START_LBA * SECTOR_SIZE;
    uint32_t stage2_lba;
    uint16_t stage2_sector_count;

    if (stage2_alloc->cluster_count == 0u)
        fail("stage2 allocation missing");

    stage2_lba = PARTITION_START_LBA + cluster_to_partition_lba(stage2_alloc->first_cluster);
    stage2_sector_count = (uint16_t)((uint32_t)stage2_alloc->cluster_count * SECTORS_PER_CLUSTER);

    memcpy(vbr + VBR_STAGE2_LBA_OFFSET, &stage2_lba, sizeof(stage2_lba));
    memcpy(vbr + VBR_STAGE2_SECTOR_COUNT_OFFSET, &stage2_sector_count, sizeof(stage2_sector_count));
}

int main(int argc, char** argv)
{
    uint8_t* image;
    uint16_t* fat_primary;
    uint16_t* fat_secondary;
    DirectoryEntry* root_entries;
    FileBlob mbr_blob;
    FileBlob vbr_blob;
    FileBlob stage2_blob;
    FileBlob kernel_blob;
    FileBlob root_test_blob;
    FileBlob demo_test_blob;
    FileBlob hello_program_blob;
    FileBlob args_program_blob;
    FileBlob big_file_blob;
    Allocation stage2_alloc;
    Allocation kernel_alloc;
    Allocation root_test_alloc;
    Allocation mydir_alloc;
    Allocation demo_alloc;
    Allocation hello_program_alloc;
    Allocation args_program_alloc;
    Allocation big_file_alloc;
    Allocation bigdir_alloc;
    uint16_t next_cluster = 2;
    uint32_t root_index = 0;
    FILE* output;

    if (argc != 10) {
        fprintf(stderr, "usage: %s <image> <mbr> <vbr> <stage2> <kernel|-> <root_test|-> <demo_test|-> <hello_program|-> <args_program|->\n", argv[0]);
        return 1;
    }

    mbr_blob = read_file_blob(argv[2]);
    vbr_blob = read_file_blob(argv[3]);
    stage2_blob = read_file_blob(argv[4]);
    kernel_blob = read_file_blob(argv[5]);
    root_test_blob = read_file_blob(argv[6]);
    demo_test_blob = read_file_blob(argv[7]);
    hello_program_blob = read_file_blob(argv[8]);
    args_program_blob = read_file_blob(argv[9]);
    big_file_blob = make_repeated_text_blob("FunnyOS big file line for FAT16 multi-cluster testing.\n", 80u);

    if (stage2_blob.size == 0)
        fail("stage2 payload is required");

    image = (uint8_t*)calloc(1, IMAGE_SIZE_BYTES);
    if (!image)
        fail("out of memory");

    write_mbr(image, &mbr_blob);
    write_vbr_and_metadata(image, &vbr_blob);

    fat_primary = (uint16_t*)(image + partition_byte_offset(FAT_START_SECTOR));
    fat_secondary = (uint16_t*)(image + partition_byte_offset(FAT_START_SECTOR + SECTORS_PER_FAT));

    stage2_alloc = allocate_blob(image, fat_primary, &stage2_blob, &next_cluster, 1);
    patch_stage2_boot_metadata(image, &stage2_alloc);
    kernel_alloc = allocate_blob(image, fat_primary, &kernel_blob, &next_cluster, 0);
    root_test_alloc = allocate_blob(image, fat_primary, &root_test_blob, &next_cluster, 0);
    mydir_alloc = allocate_blob(image, fat_primary, &(FileBlob){0}, &next_cluster, 1);
    demo_alloc = allocate_blob(image, fat_primary, &demo_test_blob, &next_cluster, 0);
    hello_program_alloc = allocate_blob(image, fat_primary, &hello_program_blob, &next_cluster, 0);
    args_program_alloc = allocate_blob(image, fat_primary, &args_program_blob, &next_cluster, 0);
    big_file_alloc = allocate_blob(image, fat_primary, &big_file_blob, &next_cluster, 0);
    bigdir_alloc = allocate_blob(image, fat_primary, &(FileBlob){0}, &next_cluster, 2);

    write_directory_chain(image, &mydir_alloc, &demo_alloc);
    write_big_directory_chain(image, &bigdir_alloc);
    memcpy(fat_secondary, fat_primary, SECTORS_PER_FAT * SECTOR_SIZE);

    root_entries = (DirectoryEntry*)(image + partition_byte_offset(ROOT_DIR_START_SECTOR));
    memset(root_entries, 0, ROOT_DIR_SECTORS * SECTOR_SIZE);

    write_dir_entry(&root_entries[root_index++], "STAGE2  BIN", FAT16_ARCHIVE, stage2_alloc.first_cluster, stage2_alloc.size);

    if (kernel_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "KERNEL  BIN", FAT16_ARCHIVE, kernel_alloc.first_cluster, kernel_alloc.size);

    if (root_test_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "TEST    TXT", FAT16_ARCHIVE, root_test_alloc.first_cluster, root_test_alloc.size);

    if (big_file_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "BIGFILE TXT", FAT16_ARCHIVE, big_file_alloc.first_cluster, big_file_alloc.size);

    if (hello_program_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "HELLO   BIN", FAT16_ARCHIVE, hello_program_alloc.first_cluster, hello_program_alloc.size);

    if (args_program_alloc.cluster_count != 0)
        write_dir_entry(&root_entries[root_index++], "ARGS    BIN", FAT16_ARCHIVE, args_program_alloc.first_cluster, args_program_alloc.size);

    write_dir_entry(&root_entries[root_index++], "MYDIR      ", FAT16_DIRECTORY, mydir_alloc.first_cluster, 0);
    write_dir_entry(&root_entries[root_index], "BIGDIR     ", FAT16_DIRECTORY, bigdir_alloc.first_cluster, 0);

    output = fopen(argv[1], "wb");
    if (!output) {
        fprintf(stderr, "imgbuild: cannot create %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    if (fwrite(image, 1, IMAGE_SIZE_BYTES, output) != IMAGE_SIZE_BYTES)
        fail("failed to write image");

    fclose(output);
    free(image);
    free_blob(&mbr_blob);
    free_blob(&vbr_blob);
    free_blob(&stage2_blob);
    free_blob(&kernel_blob);
    free_blob(&root_test_blob);
    free_blob(&demo_test_blob);
    free_blob(&hello_program_blob);
    free_blob(&args_program_blob);
    free_blob(&big_file_blob);
    return 0;
}
