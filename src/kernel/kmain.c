#include "../common/bootinfo.h"
#include "console.h"
#include "fs.h"
#include "memory.h"
#include "panic.h"
#include "shell.h"

void kmain(const BootInfo* boot_info)
{
    console_init(boot_info);

    if (boot_info == 0 || boot_info->magic != BOOTINFO_MAGIC) {
        panic("BootInfo invalid");
    }
    if (!memory_init(boot_info)) {
        panic("Memory init failed");
    }
    if (!memory_self_test()) {
        panic("Memory self-test failed");
    }

    if (!fs_init(boot_info)) {
        panic("FS init failed");
    }
    shell_run();
}
