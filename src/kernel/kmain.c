#include "../common/bootinfo.h"
#include "acpi.h"
#include "apic.h"
#include "console.h"
#include "fs.h"
#include "memory.h"
#include "panic.h"
#include "paging.h"
#include "pci.h"
#include "shell.h"
#include "timer.h"

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
    if (!pci_init()) {
        panic("PCI init failed");
    }
    if (!paging_init(boot_info)) {
        panic("Paging init failed");
    }
    if (!acpi_init(boot_info)) {
        panic("ACPI init failed");
    }
    if (!apic_init(acpi_platform_info())) {
        panic("APIC init failed");
    }
    if (!timer_init()) {
        panic("Timer init failed");
    }

    if (!fs_init(boot_info)) {
        panic("FS init failed");
    }
    shell_run();
}
