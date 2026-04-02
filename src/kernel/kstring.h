#ifndef FUNNYOS_KERNEL_KSTRING_H
#define FUNNYOS_KERNEL_KSTRING_H

#include "../common/types.h"

void* k_memcpy(void* dst, const void* src, size_t count);
void* k_memset(void* dst, int value, size_t count);
int k_memcmp(const void* lhs, const void* rhs, size_t count);
size_t k_strlen(const char* s);
int k_strcmp(const char* lhs, const char* rhs);
void k_strcpy(char* dst, const char* src);
char k_toupper(char c);
bool k_is_space(char c);

#endif
