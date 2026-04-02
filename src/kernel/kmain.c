#include "../common/bootinfo.h"
#include "ata.h"
#include "console.h"
#include "fat16.h"
#include "shell.h"

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
    console_init();

    if (boot_info == 0 || boot_info->magic != BOOTINFO_MAGIC) {
        console_write_line("BootInfo invalid");
        halt_forever();
    }

    if (!ata_init(boot_info->boot_drive)) {
        console_write_line("ATA init failed");
        halt_forever();
    }

    if (!fat16_mount(boot_info)) {
        console_write_line("FAT16 mount failed");
        halt_forever();
    }
    shell_run();
}
