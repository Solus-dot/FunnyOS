#include "shell.h"
#include "console.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "path.h"
#include "program.h"

#define SHELL_LINE_CAPACITY 128u

typedef struct FsListContext {
    bool wrote_anything;
} FsListContext;

typedef struct FsReadContext {
    bool wrote_anything;
    char last_char;
} FsReadContext;

typedef void (*ShellCommandHandler)(const char* argument);

typedef struct ShellCommand {
    const char* name;
    const char* usage;
    ShellCommandHandler handler;
} ShellCommand;

static char g_cwd[PATH_CAPACITY] = "/";

static const char* const MSG_COMMANDS = "Commands: help ls cd pwd cat clear mkdir write append rm mv";
static const char* const MSG_INVALID_PATH = "invalid path";
static const char* const MSG_NOT_FOUND = "not found";
static const char* const MSG_ALREADY_EXISTS = "already exists";
static const char* const MSG_NOT_A_DIRECTORY = "not a directory";
static const char* const MSG_NOT_A_FILE = "not a file";
static const char* const MSG_DIRECTORY_NOT_EMPTY = "directory not empty";
static const char* const MSG_FS_ERROR = "fs error";

static void shell_command_help(const char* argument);
static void shell_command_ls(const char* argument);
static void shell_command_cd(const char* argument);
static void shell_command_pwd(const char* argument);
static void shell_command_cat(const char* argument);
static void shell_command_clear(const char* argument);
static void shell_command_mkdir(const char* argument);
static void shell_command_write(const char* argument);
static void shell_command_append(const char* argument);
static void shell_command_rm(const char* argument);
static void shell_command_mv(const char* argument);

static const ShellCommand g_commands[] = {
    {"help", NULL, shell_command_help},
    {"ls", NULL, shell_command_ls},
    {"cd", "usage: cd <path>", shell_command_cd},
    {"pwd", NULL, shell_command_pwd},
    {"cat", "usage: cat <path>", shell_command_cat},
    {"clear", NULL, shell_command_clear},
    {"mkdir", "usage: mkdir <path>", shell_command_mkdir},
    {"write", "usage: write <path> <text>", shell_command_write},
    {"append", "usage: append <path> <text>", shell_command_append},
    {"rm", "usage: rm <path>", shell_command_rm},
    {"mv", "usage: mv <old> <new>", shell_command_mv},
};

static void shell_print_prompt(void)
{
    console_write("FunnyOS:");
    console_write(g_cwd);
    console_write("> ");
}

static void shell_print_usage(const char* usage)
{
    if (usage != NULL)
        console_write_line(usage);
}

static bool resolve_path_or_print(const char* input, char* out)
{
    if (!path_normalize(g_cwd, input, out, PATH_CAPACITY)) {
        console_write_line(MSG_INVALID_PATH);
        return false;
    }

    return true;
}

static const ShellCommand* shell_find_command(const char* name)
{
    uint32_t i;

    for (i = 0; i < sizeof(g_commands) / sizeof(g_commands[0]); ++i) {
        if (k_strcmp(name, g_commands[i].name) == 0)
            return &g_commands[i];
    }

    return NULL;
}

static void shell_print_fs_result(FsResult result, bool treat_dir_as_file)
{
    switch (result) {
    case FS_OK:
        break;
    case FS_NOT_FOUND:
        console_write_line(MSG_NOT_FOUND);
        break;
    case FS_ALREADY_EXISTS:
        console_write_line(MSG_ALREADY_EXISTS);
        break;
    case FS_NOT_A_DIRECTORY:
        console_write_line(MSG_NOT_A_DIRECTORY);
        break;
    case FS_IS_A_DIRECTORY:
        console_write_line(treat_dir_as_file ? MSG_NOT_A_FILE : MSG_NOT_A_DIRECTORY);
        break;
    case FS_DIRECTORY_NOT_EMPTY:
        console_write_line(MSG_DIRECTORY_NOT_EMPTY);
        break;
    case FS_INVALID_NAME:
        console_write_line(MSG_INVALID_PATH);
        break;
    case FS_NO_SPACE:
    case FS_IO_ERROR:
    default:
        console_write_line(MSG_FS_ERROR);
        break;
    }
}

static bool shell_split_write_args(char* argument, char** path_out, char** text_out)
{
    char* path;
    char* cursor;

    if (path_out == NULL || text_out == NULL)
        return false;
    if (argument == NULL)
        return false;

    while (*argument != '\0' && k_is_space(*argument))
        ++argument;
    if (*argument == '\0')
        return false;

    path = argument;
    while (*argument != '\0' && !k_is_space(*argument))
        ++argument;

    if (*argument == '\0') {
        *path_out = path;
        *text_out = argument;
        return true;
    }

    *argument++ = '\0';
    while (*argument != '\0' && k_is_space(*argument))
        ++argument;
    cursor = argument;

    *path_out = path;
    *text_out = cursor;
    return true;
}

static bool shell_split_two_args(char* argument, char** first_out, char** second_out)
{
    char* first;
    char* second;
    char* cursor;

    if (first_out == NULL || second_out == NULL)
        return false;
    if (argument == NULL)
        return false;

    while (*argument != '\0' && k_is_space(*argument))
        ++argument;
    if (*argument == '\0')
        return false;

    first = argument;
    while (*argument != '\0' && !k_is_space(*argument))
        ++argument;
    if (*argument == '\0')
        return false;

    *argument++ = '\0';
    while (*argument != '\0' && k_is_space(*argument))
        ++argument;
    if (*argument == '\0')
        return false;
    second = argument;
    cursor = second;
    while (*cursor != '\0' && !k_is_space(*cursor))
        ++cursor;
    if (*cursor != '\0')
        *cursor = '\0';

    *first_out = first;
    *second_out = second;
    return true;
}

static bool shell_print_dir_entry(const FsNodeInfo* entry, void* context)
{
    FsListContext* list_context = (FsListContext*)context;

    console_write(entry->name);
    if (entry->type == FS_NODE_DIR)
        console_write(" <DIR>");
    console_write_char('\n');
    list_context->wrote_anything = true;
    return true;
}

static bool shell_print_file_chunk(const uint8_t* data, uint32_t length, void* context)
{
    FsReadContext* read_context = (FsReadContext*)context;

    console_write_n((const char*)data, length);
    if (length != 0u) {
        read_context->wrote_anything = true;
        read_context->last_char = (char)data[length - 1u];
    }
    return true;
}

static void shell_command_help(const char* argument)
{
    (void)argument;
    console_write_line(MSG_COMMANDS);
}

static void shell_command_ls(const char* argument)
{
    char path[PATH_CAPACITY];
    FsNodeInfo node;
    FsListContext list_context;

    list_context.wrote_anything = false;

    if (argument == NULL || *argument == '\0') {
        k_strcpy(path, g_cwd);
    } else if (!resolve_path_or_print(argument, path)) {
        return;
    }

    if (fs_stat(path, &node) != FS_OK) {
        console_write_line(MSG_NOT_FOUND);
        return;
    }
    if (node.type != FS_NODE_DIR) {
        console_write_line(MSG_NOT_A_DIRECTORY);
        return;
    }
    shell_print_fs_result(fs_list_dir(path, shell_print_dir_entry, &list_context), false);
}

static void shell_command_cd(const char* argument)
{
    char path[PATH_CAPACITY];
    FsNodeInfo node;

    if (argument == NULL || *argument == '\0') {
        shell_print_usage("usage: cd <path>");
        return;
    }
    if (!resolve_path_or_print(argument, path))
        return;
    if (fs_stat(path, &node) != FS_OK) {
        console_write_line(MSG_NOT_FOUND);
        return;
    }
    if (node.type != FS_NODE_DIR) {
        console_write_line(MSG_NOT_A_DIRECTORY);
        return;
    }

    k_strcpy(g_cwd, path);
}

static void shell_command_pwd(const char* argument)
{
    (void)argument;
    console_write_line(g_cwd);
}

static void shell_command_cat(const char* argument)
{
    char path[PATH_CAPACITY];
    FsNodeInfo node;
    FsReadContext read_context;

    read_context.wrote_anything = false;
    read_context.last_char = '\0';

    if (argument == NULL || *argument == '\0') {
        shell_print_usage("usage: cat <path>");
        return;
    }
    if (!resolve_path_or_print(argument, path))
        return;
    if (fs_stat(path, &node) != FS_OK) {
        console_write_line(MSG_NOT_FOUND);
        return;
    }
    if (node.type == FS_NODE_DIR) {
        console_write_line(MSG_NOT_A_FILE);
        return;
    }
    if (fs_read_file(path, shell_print_file_chunk, &read_context) != FS_OK) {
        console_write_line(MSG_FS_ERROR);
        return;
    }
    if (!read_context.wrote_anything || read_context.last_char != '\n')
        console_write_char('\n');
}

static void shell_command_clear(const char* argument)
{
    (void)argument;
    console_clear();
}

static void shell_command_mkdir(const char* argument)
{
    char path[PATH_CAPACITY];

    if (argument == NULL || *argument == '\0') {
        shell_print_usage("usage: mkdir <path>");
        return;
    }
    if (!resolve_path_or_print(argument, path))
        return;

    shell_print_fs_result(fs_make_dir(path), false);
}

static void shell_command_write_internal(const char* argument, bool append)
{
    char path[PATH_CAPACITY];
    char mutable_argument[SHELL_LINE_CAPACITY];
    char* path_arg;
    char* text_arg;

    if (argument == NULL) {
        shell_print_usage(append ? "usage: append <path> <text>" : "usage: write <path> <text>");
        return;
    }

    k_strcpy(mutable_argument, argument);
    if (!shell_split_write_args(mutable_argument, &path_arg, &text_arg)) {
        shell_print_usage(append ? "usage: append <path> <text>" : "usage: write <path> <text>");
        return;
    }
    if (!resolve_path_or_print(path_arg, path))
        return;

    shell_print_fs_result(fs_write_file(path, (const uint8_t*)text_arg, k_strlen(text_arg), append), true);
}

static void shell_command_write(const char* argument)
{
    shell_command_write_internal(argument, false);
}

static void shell_command_append(const char* argument)
{
    shell_command_write_internal(argument, true);
}

static void shell_command_rm(const char* argument)
{
    char path[PATH_CAPACITY];

    if (argument == NULL || *argument == '\0') {
        shell_print_usage("usage: rm <path>");
        return;
    }
    if (!resolve_path_or_print(argument, path))
        return;

    shell_print_fs_result(fs_remove(path), false);
}

static void shell_command_mv(const char* argument)
{
    char old_path[PATH_CAPACITY];
    char new_path[PATH_CAPACITY];
    char mutable_argument[SHELL_LINE_CAPACITY];
    char* old_arg;
    char* new_arg;

    if (argument == NULL) {
        shell_print_usage("usage: mv <old> <new>");
        return;
    }

    k_strcpy(mutable_argument, argument);
    if (!shell_split_two_args(mutable_argument, &old_arg, &new_arg)) {
        shell_print_usage("usage: mv <old> <new>");
        return;
    }
    if (!resolve_path_or_print(old_arg, old_path))
        return;
    if (!resolve_path_or_print(new_arg, new_path))
        return;

    shell_print_fs_result(fs_rename(old_path, new_path), false);
}

static void shell_execute(char* line)
{
    char* command = line;
    char* argument = NULL;
    char* cursor;
    const ShellCommand* shell_command;
    ProgramDispatchResult dispatch_result;

    while (*command != '\0' && k_is_space(*command))
        ++command;
    if (*command == '\0')
        return;

    cursor = command;
    while (*cursor != '\0' && !k_is_space(*cursor))
        ++cursor;
    if (*cursor != '\0') {
        *cursor++ = '\0';
        while (*cursor != '\0' && k_is_space(*cursor))
            ++cursor;
        argument = cursor;
    }

    shell_command = shell_find_command(command);
    if (shell_command == NULL) {
        dispatch_result = program_dispatch(command, argument, g_cwd);
        if (dispatch_result == PROGRAM_DISPATCH_NOT_FOUND)
            console_write_line("unknown command");
        return;
    }

    shell_command->handler(argument);
}

void shell_run(void)
{
    char line[SHELL_LINE_CAPACITY];

    console_write_line("FunnyOS shell ready");

    for (;;) {
        shell_print_prompt();
        keyboard_read_line(line, sizeof(line));
        shell_execute(line);
    }
}
