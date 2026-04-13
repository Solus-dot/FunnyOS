#ifndef FUNNYOS_PROGRAM_API_H
#define FUNNYOS_PROGRAM_API_H

#include "types.h"

#define PROGRAM_INFO_MAGIC 0x4D524750u
#define PROGRAM_SYSCALL_VECTOR 0x80u

typedef enum ProgramSyscall {
    PROGRAM_SYSCALL_EXIT = 0u,
    PROGRAM_SYSCALL_WRITE = 1u,
    PROGRAM_SYSCALL_READ_LINE = 2u
} ProgramSyscall;

typedef struct ProgramInfo {
    uint32_t magic;
    uint32_t reserved;
    size_t argc;
    uintptr_t argv_addr;
    uintptr_t cwd_addr;
} ProgramInfo;

#endif
