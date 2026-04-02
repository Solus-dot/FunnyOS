#include "kstring.h"

void* k_memcpy(void* dst, const void* src, size_t count)
{
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    size_t i;

    for (i = 0; i < count; ++i)
        out[i] = in[i];

    return dst;
}

void* k_memset(void* dst, int value, size_t count)
{
    uint8_t* out = (uint8_t*)dst;
    size_t i;

    for (i = 0; i < count; ++i)
        out[i] = (uint8_t)value;

    return dst;
}

int k_memcmp(const void* lhs, const void* rhs, size_t count)
{
    const uint8_t* a = (const uint8_t*)lhs;
    const uint8_t* b = (const uint8_t*)rhs;
    size_t i;

    for (i = 0; i < count; ++i) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }

    return 0;
}

size_t k_strlen(const char* s)
{
    size_t len = 0;

    while (s[len] != '\0')
        ++len;

    return len;
}

int k_strcmp(const char* lhs, const char* rhs)
{
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs)
            return (int)(uint8_t)*lhs - (int)(uint8_t)*rhs;
        ++lhs;
        ++rhs;
    }

    return (int)(uint8_t)*lhs - (int)(uint8_t)*rhs;
}

void k_strcpy(char* dst, const char* src)
{
    while (*src != '\0') {
        *dst = *src;
        ++dst;
        ++src;
    }

    *dst = '\0';
}

char k_toupper(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));

    return c;
}

bool k_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
