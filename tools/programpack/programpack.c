#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "program_format.h"

static void fail(const char* message)
{
    fprintf(stderr, "programpack: %s\n", message);
    exit(1);
}

static uint32_t parse_u32(const char* text)
{
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 0);

    if (text == NULL || *text == '\0' || end == NULL || *end != '\0')
        fail("invalid integer argument");
    if (value > 0xFFFFFFFFul)
        fail("integer argument out of range");

    return (uint32_t)value;
}

static uint8_t* read_file(const char* path, uint32_t* size_out)
{
    FILE* fp = fopen(path, "rb");
    uint8_t* data = NULL;
    long size;

    if (!fp) {
        fprintf(stderr, "programpack: cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    if (fseek(fp, 0, SEEK_END) != 0)
        fail("failed to seek input");
    size = ftell(fp);
    if (size < 0)
        fail("failed to determine input size");
    if (fseek(fp, 0, SEEK_SET) != 0)
        fail("failed to rewind input");

    if (size > 0) {
        data = (uint8_t*)malloc((size_t)size);
        if (!data)
            fail("out of memory");
        if (fread(data, 1, (size_t)size, fp) != (size_t)size)
            fail("failed to read input");
    }

    fclose(fp);
    *size_out = (uint32_t)size;
    return data;
}

static void write_file(const char* path, const uint8_t* data, uint32_t size)
{
    FILE* fp = fopen(path, "wb");

    if (!fp) {
        fprintf(stderr, "programpack: cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    if (size != 0 && fwrite(data, 1, size, fp) != size)
        fail("failed to write output");

    fclose(fp);
}

static void command_pack(const char* input_path, const char* output_path, const char* entry_text, const char* bss_text)
{
    uint32_t image_size = 0;
    uint32_t entry_offset = parse_u32(entry_text);
    uint32_t bss_size = parse_u32(bss_text);
    uint8_t* payload = read_file(input_path, &image_size);
    uint8_t* file_data;
    ProgramHeader* header;

    if (image_size == 0)
        fail("input payload must be non-empty");
    if (entry_offset >= image_size)
        fail("entry offset must point inside the payload");

    file_data = (uint8_t*)malloc(PROGRAM_HEADER_SIZE + image_size);
    if (!file_data)
        fail("out of memory");

    header = (ProgramHeader*)file_data;
    header->magic = PROGRAM_HEADER_MAGIC;
    header->version = PROGRAM_HEADER_VERSION;
    header->header_size = PROGRAM_HEADER_SIZE;
    header->image_size = image_size;
    header->bss_size = bss_size;
    header->entry_offset = entry_offset;
    header->flags = PROGRAM_HEADER_FLAGS_NONE;

    memcpy(file_data + PROGRAM_HEADER_SIZE, payload, image_size);
    write_file(output_path, file_data, PROGRAM_HEADER_SIZE + image_size);

    free(file_data);
    free(payload);
}

static void command_inspect(const char* input_path)
{
    uint32_t file_size = 0;
    uint8_t* file_data = read_file(input_path, &file_size);
    const ProgramHeader* header;

    if (file_size < PROGRAM_HEADER_SIZE)
        fail("file is smaller than the program header");

    header = (const ProgramHeader*)file_data;
    printf("magic=0x%08X\n", header->magic);
    printf("version=%u\n", header->version);
    printf("header_size=%u\n", header->header_size);
    printf("image_size=%u\n", header->image_size);
    printf("bss_size=%u\n", header->bss_size);
    printf("entry_offset=%u\n", header->entry_offset);
    printf("flags=%u\n", header->flags);
    printf("file_size=%u\n", file_size);

    free(file_data);
}

int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "pack") == 0) {
        if (argc != 5 && argc != 6)
            fail("usage: programpack pack <input> <output> <entry_offset> [bss_size]");
        command_pack(argv[2], argv[3], argv[4], argc == 6 ? argv[5] : "0");
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "inspect") == 0) {
        command_inspect(argv[2]);
        return 0;
    }

    fail("usage: programpack pack <input> <output> <entry_offset> [bss_size] | inspect <file>");
    return 1;
}
