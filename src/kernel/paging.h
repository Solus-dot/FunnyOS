#ifndef FUNNYOS_KERNEL_PAGING_H
#define FUNNYOS_KERNEL_PAGING_H

#include "../common/bootinfo.h"
#include "../common/types.h"

bool paging_init(const BootInfo* boot_info);
bool paging_map_range(uintptr_t virt_start, uintptr_t phys_start, size_t size, bool writable, bool executable);

#endif
