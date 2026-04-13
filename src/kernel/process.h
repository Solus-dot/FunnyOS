#ifndef FUNNYOS_KERNEL_PROCESS_H
#define FUNNYOS_KERNEL_PROCESS_H

#include "../common/program_api.h"
#include "../common/types.h"
#include "path.h"

typedef enum ProcessState {
    PROCESS_STATE_IDLE,
    PROCESS_STATE_LOADED,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_EXITED
} ProcessState;

typedef struct ProcessAddressSpace {
    uintptr_t image_base;
    uintptr_t stack_base;
    uintptr_t stack_top;
    size_t image_capacity;
    size_t image_page_count;
    void* image_backing;
    size_t stack_size;
    size_t stack_page_count;
    void* stack_backing;
    size_t file_capacity;
    size_t file_page_count;
    void* file_buffer;
} ProcessAddressSpace;

typedef struct ProcessImage {
    uintptr_t entry_point;
    uint32_t file_size;
    uint8_t* file_buffer;
} ProcessImage;

typedef struct ProcessRuntime {
    ProgramApi api;
    ProgramInfo info;
    char cwd[PATH_CAPACITY];
    char arg_buffer[256];
    uintptr_t argv[8];
    uint32_t exit_status;
    bool exit_requested;
} ProcessRuntime;

typedef struct Process {
    ProcessState state;
    ProcessAddressSpace address_space;
    ProcessImage image;
    ProcessRuntime runtime;
} Process;

void process_init(Process* process);
bool process_run_foreground(Process* process, const char* path, const char* command, const char* argument_line, const char* cwd);
Process* process_foreground(void);

#endif
