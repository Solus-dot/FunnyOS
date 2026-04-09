#ifndef FUNNYOS_KERNEL_KEYBOARD_H
#define FUNNYOS_KERNEL_KEYBOARD_H

#include "../common/types.h"

size_t keyboard_read_line(char* buffer, size_t capacity);

#endif
