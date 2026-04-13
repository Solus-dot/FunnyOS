#include "program.h"
#include "console.h"
#include "fs.h"
#include "kstring.h"
#include "path.h"
#include "process.h"

#define PROGRAM_EXTENSION ".ELF"
#define PROGRAM_EXTENSION_LEN 4u

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
        && k_toupper(path[length - 3u]) == 'E'
        && k_toupper(path[length - 2u]) == 'L'
        && k_toupper(path[length - 1u]) == 'F';
}

static bool build_lookup_path(const char* base, const char* command, char* out, uint32_t capacity)
{
    char with_extension[PATH_CAPACITY];
    uint32_t command_len = (uint32_t)k_strlen(command);

    if (path_has_program_suffix(command))
        return path_normalize(base, command, out, capacity);
    if (command_len + PROGRAM_EXTENSION_LEN + 1u > sizeof(with_extension))
        return false;

    k_strcpy(with_extension, command);
    k_strcpy(with_extension + command_len, PROGRAM_EXTENSION);
    return path_normalize(base, with_extension, out, capacity);
}

static bool program_run_path(const char* path, const char* command, const char* argument_line, const char* cwd)
{
    return process_run_foreground(process_foreground(), path, command, argument_line, cwd);
}

ProgramDispatchResult program_dispatch(const char* command, const char* argument_line, const char* cwd)
{
    char path[PATH_CAPACITY];

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
            if (fs_stat(path, &(FsNodeInfo){0}) != FS_OK)
                console_write_line("program not found");
            return PROGRAM_DISPATCH_FAILED;
        }
        return PROGRAM_DISPATCH_EXECUTED;
    }

    if (build_lookup_path("/", command, path, sizeof(path)) && fs_stat(path, &(FsNodeInfo){0}) == FS_OK) {
        if (!program_run_path(path, command, argument_line, cwd))
            return PROGRAM_DISPATCH_FAILED;
        return PROGRAM_DISPATCH_EXECUTED;
    }

    if (build_lookup_path(cwd, command, path, sizeof(path)) && fs_stat(path, &(FsNodeInfo){0}) == FS_OK) {
        if (!program_run_path(path, command, argument_line, cwd))
            return PROGRAM_DISPATCH_FAILED;
        return PROGRAM_DISPATCH_EXECUTED;
    }

    return PROGRAM_DISPATCH_NOT_FOUND;
}
