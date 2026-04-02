#ifndef FUNNYOS_KERNEL_KEYBOARD_H
#define FUNNYOS_KERNEL_KEYBOARD_H

#include "../common/types.h"

uint32_t keyboard_read_line(char* buffer, uint32_t capacity);

#endif
