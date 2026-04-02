#include "shell.h"
#include "console.h"
#include "fat16.h"
#include "keyboard.h"
#include "kstring.h"

#define SHELL_PATH_CAPACITY 128u
#define SHELL_LINE_CAPACITY 128u
#define SHELL_COMPONENT_LIMIT 16u

typedef struct ListContext {
    bool had_entries;
} ListContext;

typedef struct CatContext {
    bool wrote_anything;
    char last_char;
} CatContext;

static char g_cwd[SHELL_PATH_CAPACITY] = "/";

static void shell_print_prompt(void)
{
    console_write("FunnyOS:");
    console_write(g_cwd);
    console_write("> ");
}

static void shell_print_usage(const char* usage)
{
    console_write_line(usage);
}

static bool push_component(char components[SHELL_COMPONENT_LIMIT][13], uint32_t* count, const char* start, uint32_t len)
{
    uint32_t i;

    if (*count >= SHELL_COMPONENT_LIMIT || len == 0 || len > 12u)
        return false;

    for (i = 0; i < len; ++i)
        components[*count][i] = k_toupper(start[i]);
    components[*count][len] = '\0';
    ++(*count);
    return true;
}

static bool normalize_path(const char* cwd, const char* input, char* out, uint32_t capacity)
{
    char components[SHELL_COMPONENT_LIMIT][13];
    uint32_t count = 0;
    const char* cursor;
    uint32_t i;
    uint32_t pos = 0;

    if (input == NULL || out == NULL || capacity < 2u)
        return false;

    if (input[0] != '/') {
        cursor = cwd;
        while (*cursor == '/')
            ++cursor;
        while (*cursor != '\0') {
            const char* slash = cursor;
            uint32_t len = 0;

            while (*slash != '\0' && *slash != '/') {
                ++slash;
                ++len;
            }

            if (!push_component(components, &count, cursor, len))
                return false;

            while (*slash == '/')
                ++slash;
            cursor = slash;
        }
    }

    cursor = input;
    while (*cursor == '/')
        ++cursor;

    while (*cursor != '\0') {
        const char* slash = cursor;
        uint32_t len = 0;

        while (*slash != '\0' && *slash != '/') {
            ++slash;
            ++len;
        }

        if (len == 1u && cursor[0] == '.') {
        } else if (len == 2u && cursor[0] == '.' && cursor[1] == '.') {
            if (count != 0)
                --count;
        } else if (!push_component(components, &count, cursor, len)) {
            return false;
        }

        while (*slash == '/')
            ++slash;
        cursor = slash;
    }

    out[pos++] = '/';
    if (count == 0u) {
        out[pos] = '\0';
        return true;
    }

    for (i = 0; i < count; ++i) {
        uint32_t j = 0;

        while (components[i][j] != '\0') {
            if (pos + 1u >= capacity)
                return false;
            out[pos++] = components[i][j++];
        }

        if (i + 1u < count) {
            if (pos + 1u >= capacity)
                return false;
            out[pos++] = '/';
        }
    }

    out[pos] = '\0';
    return true;
}

static bool print_dir_entry(const FatDirEntry* entry, void* context)
{
    ListContext* list_context = (ListContext*)context;

    console_write(entry->name);
    if (entry->is_dir)
        console_write(" <DIR>");
    console_write_char('\n');
    list_context->had_entries = true;
    return true;
}

static bool print_file_chunk(const uint8_t* data, uint32_t length, void* context)
{
    CatContext* cat_context = (CatContext*)context;

    console_write_n((const char*)data, length);
    if (length != 0u) {
        cat_context->wrote_anything = true;
        cat_context->last_char = (char)data[length - 1u];
    }
    return true;
}

static void shell_command_help(void)
{
    console_write_line("Commands: help ls cd pwd cat clear");
}

static void shell_command_pwd(void)
{
    console_write_line(g_cwd);
}

static void shell_command_ls(const char* arg)
{
    char path[SHELL_PATH_CAPACITY];
    FatDirEntry stat_entry;
    ListContext list_context = {false};

    if (arg == NULL || *arg == '\0') {
        k_strcpy(path, g_cwd);
    } else if (!normalize_path(g_cwd, arg, path, sizeof(path))) {
        console_write_line("invalid path");
        return;
    }

    if (!fat16_stat(path, &stat_entry)) {
        console_write_line("not found");
        return;
    }

    if (!stat_entry.is_dir) {
        console_write_line("not a directory");
        return;
    }

    if (!fat16_list_dir(path, print_dir_entry, &list_context))
        console_write_line("fs error");
}

static void shell_command_cd(const char* arg)
{
    char path[SHELL_PATH_CAPACITY];
    FatDirEntry stat_entry;

    if (arg == NULL || *arg == '\0') {
        shell_print_usage("usage: cd <path>");
        return;
    }
    if (!normalize_path(g_cwd, arg, path, sizeof(path))) {
        console_write_line("invalid path");
        return;
    }
    if (!fat16_stat(path, &stat_entry)) {
        console_write_line("not found");
        return;
    }
    if (!stat_entry.is_dir) {
        console_write_line("not a directory");
        return;
    }

    k_strcpy(g_cwd, path);
}

static void shell_command_cat(const char* arg)
{
    char path[SHELL_PATH_CAPACITY];
    FatDirEntry stat_entry;
    CatContext cat_context = {false, '\0'};

    if (arg == NULL || *arg == '\0') {
        shell_print_usage("usage: cat <path>");
        return;
    }
    if (!normalize_path(g_cwd, arg, path, sizeof(path))) {
        console_write_line("invalid path");
        return;
    }
    if (!fat16_stat(path, &stat_entry)) {
        console_write_line("not found");
        return;
    }
    if (stat_entry.is_dir) {
        console_write_line("not a file");
        return;
    }
    if (!fat16_read_file(path, print_file_chunk, &cat_context)) {
        console_write_line("fs error");
        return;
    }
    if (!cat_context.wrote_anything || cat_context.last_char != '\n')
        console_write_char('\n');
}

static void shell_execute(char* line)
{
    char* command = line;
    char* argument = NULL;
    char* cursor;

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

    if (k_strcmp(command, "help") == 0) {
        shell_command_help();
    } else if (k_strcmp(command, "pwd") == 0) {
        shell_command_pwd();
    } else if (k_strcmp(command, "ls") == 0) {
        shell_command_ls(argument);
    } else if (k_strcmp(command, "cd") == 0) {
        shell_command_cd(argument);
    } else if (k_strcmp(command, "cat") == 0) {
        shell_command_cat(argument);
    } else if (k_strcmp(command, "clear") == 0) {
        console_clear();
    } else {
        console_write_line("unknown command");
    }
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
