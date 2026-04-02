#ifndef FUNNYOS_KERNEL_PROGRAM_H
#define FUNNYOS_KERNEL_PROGRAM_H

#include "../common/types.h"

typedef enum ProgramDispatchResult {
    PROGRAM_DISPATCH_EXECUTED,
    PROGRAM_DISPATCH_NOT_FOUND,
    PROGRAM_DISPATCH_FAILED
} ProgramDispatchResult;

ProgramDispatchResult program_dispatch(const char* command, const char* argument_line, const char* cwd);

#endif
