#ifndef FUNNYOS_KERNEL_PAGING_H
#define FUNNYOS_KERNEL_PAGING_H

#include "../common/bootinfo.h"
#include "../common/types.h"

bool paging_init(const BootInfo* boot_info);
bool paging_map_range(uintptr_t virt_start, uintptr_t phys_start, size_t size, bool writable, bool executable);
uintptr_t paging_kernel_root(void);
uintptr_t paging_current_root(void);
void paging_activate_root(uintptr_t root);
uintptr_t paging_create_address_space(void);
void paging_destroy_address_space(uintptr_t root);
bool paging_map_user_range(uintptr_t root, uintptr_t virt_start, uintptr_t phys_start, size_t size, bool writable, bool executable);

#endif
