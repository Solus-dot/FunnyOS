#ifndef FUNNYOS_PROGRAM_API_H
#define FUNNYOS_PROGRAM_API_H

#include "types.h"

#define PROGRAM_INFO_MAGIC 0x4D524750u

typedef struct ProgramInfo {
    uint32_t magic;
    uint32_t argc;
    uint32_t argv_addr;
    uint32_t cwd_addr;
} ProgramInfo;

typedef struct ProgramApi {
    void (*exit)(uint32_t status);
    void (*write)(const char* data, uint32_t len);
    uint32_t (*read_line)(char* buf, uint32_t cap);
} ProgramApi;

#endif
