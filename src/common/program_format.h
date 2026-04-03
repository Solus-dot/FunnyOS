#ifndef FUNNYOS_PROGRAM_FORMAT_H
#define FUNNYOS_PROGRAM_FORMAT_H

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#include <stdint.h>
#else
#include "types.h"
#endif

#define PROGRAM_HEADER_MAGIC 0x31455846u
#define PROGRAM_HEADER_VERSION 1u
#define PROGRAM_HEADER_FLAGS_NONE 0u

typedef struct __attribute__((packed)) ProgramHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t flags;
} ProgramHeader;

#define PROGRAM_HEADER_SIZE ((uint16_t)sizeof(ProgramHeader))

#endif
