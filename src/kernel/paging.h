#ifndef FUNNYOS_KERNEL_PAGING_H
#define FUNNYOS_KERNEL_PAGING_H

#include "../common/bootinfo.h"
#include "../common/types.h"

bool paging_init(const BootInfo* boot_info);

#endif
