#include "program.h"
#include "../common/program_api.h"
#include "console.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "path.h"

#define PROGRAM_EXTENSION ".BIN"
#define PROGRAM_EXTENSION_LEN 4u
#define PROGRAM_LOAD_ADDR 0x00200000u
#define PROGRAM_STACK_TOP 0x00210000u
#define PROGRAM_MAX_SIZE 65536u
#define PROGRAM_MAX_ARGS 8u
#define PROGRAM_ARG_BUFFER_SIZE 256u

extern uint32_t program_invoke(uint32_t entry_point, uint32_t api_ptr, uint32_t info_ptr, uint32_t stack_top);
extern void program_exit_resume(void);

typedef struct ProgramLoadContext {
    uint8_t* dst;
    uint32_t capacity;
    uint32_t size;
} ProgramLoadContext;

static ProgramApi g_program_api;
static ProgramInfo g_program_info;
static char g_program_cwd[PATH_CAPACITY];
static char g_program_arg_buffer[PROGRAM_ARG_BUFFER_SIZE];
static uint32_t g_program_argv[PROGRAM_MAX_ARGS];
static uint32_t g_program_exit_status = 0;
static bool g_program_exit_requested = false;

static void program_api_write(const char* data, uint32_t len)
{
    if (data == NULL)
        return;

    console_write_n(data, len);
}

static uint32_t program_api_read_line(char* buf, uint32_t cap)
{
    if (buf == NULL)
        return 0;

    return keyboard_read_line(buf, cap);
}

static void program_api_exit(uint32_t status)
{
    g_program_exit_requested = true;
    g_program_exit_status = status;
    __asm__ volatile("jmp program_exit_resume");
}

static void program_init_api(void)
{
    g_program_api.exit = program_api_exit;
    g_program_api.write = program_api_write;
    g_program_api.read_line = program_api_read_line;
}

static void console_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0;

    if (value == 0u) {
        console_write_char('0');
        return;
    }

    while (value != 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count != 0u)
        console_write_char(digits[--count]);
}

static bool program_file_chunk_copy(const uint8_t* data, uint32_t length, void* context)
{
    ProgramLoadContext* load_context = (ProgramLoadContext*)context;

    if (load_context->size + length > load_context->capacity)
        return false;

    k_memcpy(load_context->dst + load_context->size, data, length);
    load_context->size += length;
    return true;
}

static bool path_has_separator(const char* path)
{
    while (*path != '\0') {
        if (*path == '/')
            return true;
        ++path;
    }

    return false;
}

static bool path_has_program_suffix(const char* path)
{
    size_t length = k_strlen(path);

    if (length < PROGRAM_EXTENSION_LEN)
        return false;

    return k_toupper(path[length - 4u]) == '.'
        && k_toupper(path[length - 3u]) == 'B'
        && k_toupper(path[length - 2u]) == 'I'
        && k_toupper(path[length - 1u]) == 'N';
}

static bool build_lookup_path(const char* base, const char* command, char* out, uint32_t capacity)
{
    char with_extension[PATH_CAPACITY];
    uint32_t command_len = k_strlen(command);

    if (command_len + PROGRAM_EXTENSION_LEN + 1u > sizeof(with_extension))
        return false;

    k_strcpy(with_extension, command);
    k_strcpy(with_extension + command_len, PROGRAM_EXTENSION);
    return path_normalize(base, with_extension, out, capacity);
}

static bool tokenize_args(const char* command, const char* argument_line)
{
    const char* cursor;
    uint32_t argc = 0;
    uint32_t offset = 0;

    k_memset(g_program_arg_buffer, 0, sizeof(g_program_arg_buffer));
    k_memset(g_program_argv, 0, sizeof(g_program_argv));

    if (command == NULL || *command == '\0')
        return false;

    cursor = command;
    while (*cursor != '\0') {
        if (offset + 1u >= sizeof(g_program_arg_buffer))
            return false;
        g_program_arg_buffer[offset++] = *cursor++;
    }
    g_program_arg_buffer[offset++] = '\0';
    g_program_argv[argc++] = (uint32_t)(uintptr_t)g_program_arg_buffer;

    cursor = argument_line;
    if (cursor == NULL)
        cursor = "";

    while (*cursor != '\0') {
        uint32_t arg_start;

        while (*cursor != '\0' && k_is_space(*cursor))
            ++cursor;
        if (*cursor == '\0')
            break;
        if (argc >= PROGRAM_MAX_ARGS)
            return false;

        arg_start = offset;
        while (*cursor != '\0' && !k_is_space(*cursor)) {
            if (offset + 1u >= sizeof(g_program_arg_buffer))
                return false;
            g_program_arg_buffer[offset++] = *cursor++;
        }
        g_program_arg_buffer[offset++] = '\0';
        g_program_argv[argc++] = (uint32_t)(uintptr_t)(g_program_arg_buffer + arg_start);
    }

    g_program_info.magic = PROGRAM_INFO_MAGIC;
    g_program_info.argc = argc;
    g_program_info.argv_addr = (uint32_t)(uintptr_t)g_program_argv;
    g_program_info.cwd_addr = (uint32_t)(uintptr_t)g_program_cwd;
    return true;
}

static bool program_load_binary(const char* path, uint32_t* size_out)
{
    FsNodeInfo node;
    ProgramLoadContext load_context;

    if (!fs_stat(path, &node))
        return false;
    if (node.type != FS_NODE_FILE || node.size == 0u) {
        console_write_line("program load failed");
        return false;
    }
    if (node.size > PROGRAM_MAX_SIZE) {
        console_write_line("program too large");
        return false;
    }

    load_context.dst = (uint8_t*)(uintptr_t)PROGRAM_LOAD_ADDR;
    load_context.capacity = PROGRAM_MAX_SIZE;
    load_context.size = 0;
    k_memset(load_context.dst, 0, PROGRAM_MAX_SIZE);

    if (!fs_read_file(path, program_file_chunk_copy, &load_context)) {
        console_write_line("program load failed");
        return false;
    }
    if (load_context.size != node.size || load_context.size == 0u) {
        console_write_line("program load failed");
        return false;
    }

    *size_out = load_context.size;
    return true;
}

static bool program_run_path(const char* path, const char* command, const char* argument_line, const char* cwd)
{
    uint32_t size_ignored;
    uint32_t invoke_result;

    if (!tokenize_args(command, argument_line)) {
        console_write_line("program load failed");
        return false;
    }

    k_strcpy(g_program_cwd, cwd);
    if (!program_load_binary(path, &size_ignored))
        return false;

    g_program_exit_requested = false;
    g_program_exit_status = 0;
    invoke_result = program_invoke(
        PROGRAM_LOAD_ADDR,
        (uint32_t)(uintptr_t)&g_program_api,
        (uint32_t)(uintptr_t)&g_program_info,
        PROGRAM_STACK_TOP);

    if (invoke_result == 0u) {
        console_write_line("program returned unexpectedly");
        return false;
    }
    if (!g_program_exit_requested) {
        console_write_line("program returned unexpectedly");
        return false;
    }
    if (g_program_exit_status != 0u) {
        console_write("program exited with status ");
        console_write_u32(g_program_exit_status);
        console_write_char('\n');
        return false;
    }

    return true;
}

ProgramDispatchResult program_dispatch(const char* command, const char* argument_line, const char* cwd)
{
    char path[PATH_CAPACITY];

    program_init_api();

    if (command == NULL || cwd == NULL || *command == '\0')
        return PROGRAM_DISPATCH_NOT_FOUND;

    if (path_has_separator(command)) {
        if (!path_normalize(cwd, command, path, sizeof(path))) {
            console_write_line("program not found");
            return PROGRAM_DISPATCH_FAILED;
        }
        if (!path_has_program_suffix(path)) {
            console_write_line("program not found");
            return PROGRAM_DISPATCH_FAILED;
        }
        if (!program_run_path(path, command, argument_line, cwd)) {
            if (!fs_stat(path, &(FsNodeInfo){0}))
                console_write_line("program not found");
            return PROGRAM_DISPATCH_FAILED;
        }
        return PROGRAM_DISPATCH_EXECUTED;
    }

    if (build_lookup_path("/", command, path, sizeof(path)) && fs_stat(path, &(FsNodeInfo){0})) {
        if (!program_run_path(path, command, argument_line, cwd))
            return PROGRAM_DISPATCH_FAILED;
        return PROGRAM_DISPATCH_EXECUTED;
    }

    if (build_lookup_path(cwd, command, path, sizeof(path)) && fs_stat(path, &(FsNodeInfo){0})) {
        if (!program_run_path(path, command, argument_line, cwd))
            return PROGRAM_DISPATCH_FAILED;
        return PROGRAM_DISPATCH_EXECUTED;
    }

    return PROGRAM_DISPATCH_NOT_FOUND;
}
