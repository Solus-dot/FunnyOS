#include "../common/bootinfo.h"

static void halt_forever(void)
{
    for (;;) {
#if defined(__i386__)
        __asm__ volatile("hlt");
#endif
    }
}

void kmain(const BootInfo* boot_info)
{
    if (boot_info == 0 || boot_info->magic != BOOTINFO_MAGIC)
        halt_forever();

    halt_forever();
}
