#include "path.h"
#include "kstring.h"

static bool push_component(char* out, uint32_t* pos, uint32_t capacity, uint32_t starts[PATH_CAPACITY], uint32_t* count, const char* start, uint32_t len)
{
    uint32_t i;

    if (len == 0u || *count >= PATH_CAPACITY)
        return false;

    if (*pos != 1u) {
        if (*pos + 1u >= capacity)
            return false;
        out[(*pos)++] = '/';
    }
    if (*pos + len >= capacity)
        return false;

    starts[*count] = *pos;
    for (i = 0; i < len; ++i)
        out[(*pos)++] = k_toupper(start[i]);
    out[*pos] = '\0';
    ++(*count);
    return true;
}

static void pop_component(char* out, uint32_t* pos, uint32_t starts[PATH_CAPACITY], uint32_t* count)
{
    uint32_t start;

    if (*count == 0u) {
        *pos = 1u;
        out[1] = '\0';
        return;
    }

    start = starts[--(*count)];
    *pos = start > 1u ? start - 1u : 1u;
    out[*pos] = '\0';
}

static bool absorb_path(char* out, uint32_t* pos, uint32_t capacity, uint32_t starts[PATH_CAPACITY], uint32_t* count, const char* path)
{
    const char* cursor;

    while (*path == '/')
        ++path;

    cursor = path;
    while (*cursor != '\0') {
        const char* slash = cursor;
        uint32_t len = 0;

        while (*slash != '\0' && *slash != '/') {
            ++slash;
            ++len;
        }

        if (len == 1u && cursor[0] == '.') {
        } else if (len == 2u && cursor[0] == '.' && cursor[1] == '.') {
            pop_component(out, pos, starts, count);
        } else if (!push_component(out, pos, capacity, starts, count, cursor, len)) {
            return false;
        }

        while (*slash == '/')
            ++slash;
        cursor = slash;
    }

    return true;
}

bool path_normalize(const char* cwd, const char* input, char* out, uint32_t capacity)
{
    uint32_t starts[PATH_CAPACITY];
    uint32_t count = 0;
    uint32_t pos = 1u;

    if (cwd == NULL || input == NULL || out == NULL || capacity < 2u)
        return false;

    out[0] = '/';
    out[1] = '\0';

    if (input[0] != '/' && !absorb_path(out, &pos, capacity, starts, &count, cwd))
        return false;
    if (!absorb_path(out, &pos, capacity, starts, &count, input))
        return false;

    if (count == 0u) {
        out[pos] = '\0';
        return true;
    }

    out[pos] = '\0';
    return true;
}
