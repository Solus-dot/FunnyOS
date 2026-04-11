#ifndef FUNNYOS_KERNEL_MEMORY_H
#define FUNNYOS_KERNEL_MEMORY_H

#include "../common/bootinfo.h"
#include "../common/types.h"

bool memory_init(const BootInfo* boot_info);
bool memory_self_test(void);
void* alloc_pages(size_t page_count);
void free_pages(void* ptr, size_t page_count);
void* kmalloc(size_t size);
void kfree(void* ptr);
size_t memory_free_pages(void);
size_t memory_total_pages(void);
void memory_dump_stats(void);

#endif
