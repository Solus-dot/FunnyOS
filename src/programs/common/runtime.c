#include "runtime.h"

const ProgramInfo* g_program_info = (const ProgramInfo*)0;

static uint64_t program_syscall1(uint64_t number, uintptr_t arg0)
{
    uint64_t result;

    __asm__ volatile(
        "int %1"
        : "=a"(result)
        : "i"(PROGRAM_SYSCALL_VECTOR), "a"(number), "D"(arg0)
        : "cc", "memory");
    return result;
}

static uint64_t program_syscall2(uint64_t number, uintptr_t arg0, uintptr_t arg1)
{
    uint64_t result;

    __asm__ volatile(
        "int %1"
        : "=a"(result)
        : "i"(PROGRAM_SYSCALL_VECTOR), "a"(number), "D"(arg0), "S"(arg1)
        : "cc", "memory");
    return result;
}

size_t program_strlen(const char* s)
{
    size_t len = 0;

    while (s[len] != '\0')
        ++len;

    return len;
}

void program_exit(uint32_t status)
{
    (void)program_syscall1(PROGRAM_SYSCALL_EXIT, (uintptr_t)status);
    for (;;)
        __asm__ volatile("hlt");
}

void program_write(const char* data, size_t len)
{
    if (data == NULL || len == 0u)
        return;

    (void)program_syscall2(PROGRAM_SYSCALL_WRITE, (uintptr_t)data, (uintptr_t)len);
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
    if (buf == NULL || cap == 0u)
        return 0u;

    return (size_t)program_syscall2(PROGRAM_SYSCALL_READ_LINE, (uintptr_t)buf, (uintptr_t)cap);
}
