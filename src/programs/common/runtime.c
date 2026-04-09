#include "runtime.h"

const ProgramApi* g_program_api = (const ProgramApi*)0;
const ProgramInfo* g_program_info = (const ProgramInfo*)0;

size_t program_strlen(const char* s)
{
    size_t len = 0;

    while (s[len] != '\0')
        ++len;

    return len;
}

void program_write(const char* data, size_t len)
{
    g_program_api->write(data, len);
}

void program_write_str(const char* s)
{
    program_write(s, program_strlen(s));
}

void program_write_line(const char* s)
{
    program_write_str(s);
    program_write("\n", 1);
}

void program_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0;

    if (value == 0u) {
        program_write("0", 1);
        return;
    }

    while (value != 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count != 0u)
        program_write(&digits[--count], 1);
}

size_t program_read_line(char* buf, size_t cap)
{
    return g_program_api->read_line(buf, cap);
}
