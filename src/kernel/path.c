#include "path.h"
#include "kstring.h"

#define PATH_COMPONENT_LIMIT 16u
#define PATH_COMPONENT_CAPACITY 13u

static bool push_component(char components[PATH_COMPONENT_LIMIT][PATH_COMPONENT_CAPACITY], uint32_t* count, const char* start, uint32_t len)
{
    uint32_t i;

    if (*count >= PATH_COMPONENT_LIMIT || len == 0u || len > 12u)
        return false;

    for (i = 0; i < len; ++i)
        components[*count][i] = k_toupper(start[i]);
    components[*count][len] = '\0';
    ++(*count);
    return true;
}

bool path_normalize(const char* cwd, const char* input, char* out, uint32_t capacity)
{
    char components[PATH_COMPONENT_LIMIT][PATH_COMPONENT_CAPACITY];
    uint32_t count = 0;
    const char* cursor;
    uint32_t i;
    uint32_t pos = 0;

    if (cwd == NULL || input == NULL || out == NULL || capacity < 2u)
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
            if (count != 0u)
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
