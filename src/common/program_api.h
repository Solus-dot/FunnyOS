#ifndef FUNNYOS_PROGRAM_API_H
#define FUNNYOS_PROGRAM_API_H

#include "types.h"

#define PROGRAM_INFO_MAGIC 0x4D524750u

typedef struct ProgramInfo {
    uint32_t magic;
    uint32_t reserved;
    size_t argc;
    uintptr_t argv_addr;
    uintptr_t cwd_addr;
} ProgramInfo;

typedef struct ProgramApi {
    void (*exit)(uint32_t status);
    void (*write)(const char* data, size_t len);
    size_t (*read_line)(char* buf, size_t cap);
} ProgramApi;

#endif
