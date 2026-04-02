#ifndef FUNNYOS_KERNEL_CONSOLE_H
#define FUNNYOS_KERNEL_CONSOLE_H

#include "../common/types.h"

void console_init(void);
void console_write_char(char c);
void console_write_n(const char* s, size_t count);
void console_write(const char* s);
void console_write_line(const char* s);
void console_clear(void);
void console_backspace(void);

#endif
