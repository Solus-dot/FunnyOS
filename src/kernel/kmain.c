#include "../common/bootinfo.h"
#include "console.h"
#include "fs.h"
#include "shell.h"

static void halt_forever(void)
{
    for (;;) {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("hlt");
#endif
    }
}

void kmain(const BootInfo* boot_info)
{
    console_init();

    if (boot_info == 0 || boot_info->magic != BOOTINFO_MAGIC) {
        console_write_line("BootInfo invalid");
        halt_forever();
    }

    if (!fs_init(boot_info)) {
        console_write_line("FS init failed");
        halt_forever();
    }
    shell_run();
}
