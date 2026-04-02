#ifndef FUNNYOS_KERNEL_PATH_H
#define FUNNYOS_KERNEL_PATH_H

#include "../common/types.h"

#define PATH_CAPACITY 128u

bool path_normalize(const char* cwd, const char* input, char* out, uint32_t capacity);

#endif
