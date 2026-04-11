#include "paging.h"
#include "memory.h"
#include "kstring.h"

#define PAGE_SIZE_4K 0x1000ull
#define PAGE_SIZE_2M 0x200000ull
#define PAGE_TABLE_ENTRIES 512u
#define PAGE_PRESENT 0x001ull
#define PAGE_WRITABLE 0x002ull
#define PAGE_PAGE_SIZE 0x080ull
#define PAGE_NX (1ull << 63)
#define CR0_WRITE_PROTECT (1ull << 16)
#define EFER_MSR 0xC0000080u
#define EFER_NXE (1ull << 11)
#define LOW_IDENTITY_LIMIT 0x0000000100000000ull

extern uint8_t _start;
extern uint8_t __text_start;
extern uint8_t __text_end;
extern uint8_t __rodata_start;
extern uint8_t __rodata_end;
extern uint8_t __data_start;
extern uint8_t __data_end;
extern uint8_t __bss_start;
extern uint8_t __kernel_image_end;

static uint64_t* g_pml4 = NULL;

static uintptr_t align_down_page(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static uintptr_t align_up_page(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint64_t paging_default_leaf_flags(bool writable)
{
    return PAGE_PRESENT | (writable ? PAGE_WRITABLE : 0u);
}

static uint64_t* alloc_page_table(void)
{
    uint64_t* table = (uint64_t*)alloc_pages(1u);

    if (table == NULL)
        return NULL;
    k_memset(table, 0, PAGE_SIZE_4K);
    return table;
}

static size_t pml4_index(uintptr_t addr)
{
    return (size_t)((addr >> 39u) & 0x1FFu);
}

static size_t pdpt_index(uintptr_t addr)
{
    return (size_t)((addr >> 30u) & 0x1FFu);
}

static size_t pd_index(uintptr_t addr)
{
    return (size_t)((addr >> 21u) & 0x1FFu);
}

static size_t pt_index(uintptr_t addr)
{
    return (size_t)((addr >> 12u) & 0x1FFu);
}

static uint64_t* get_or_create_table(uint64_t* parent, size_t index)
{
    uint64_t entry = parent[index];
    uint64_t* table;

    if ((entry & PAGE_PRESENT) != 0u)
        return (uint64_t*)(uintptr_t)(entry & ~0xFFFull);

    table = alloc_page_table();
    if (table == NULL)
        return NULL;
    parent[index] = (uint64_t)(uintptr_t)table | PAGE_PRESENT | PAGE_WRITABLE;
    return table;
}

static uint64_t* get_or_create_pd(uintptr_t virt)
{
    uint64_t* pdpt = get_or_create_table(g_pml4, pml4_index(virt));

    if (pdpt == NULL)
        return NULL;
    return get_or_create_table(pdpt, pdpt_index(virt));
}

static bool split_large_page(uint64_t* pd, size_t pd_slot)
{
    uint64_t entry = pd[pd_slot];
    uint64_t* pt;
    uintptr_t base;
    uint64_t flags;
    size_t index;

    if ((entry & PAGE_PRESENT) == 0u || (entry & PAGE_PAGE_SIZE) == 0u)
        return true;

    pt = alloc_page_table();
    if (pt == NULL)
        return false;

    base = (uintptr_t)(entry & ~0x1FFFFFull);
    flags = entry & (PAGE_PRESENT | PAGE_WRITABLE | PAGE_NX);
    for (index = 0u; index < PAGE_TABLE_ENTRIES; ++index)
        pt[index] = (uint64_t)(base + index * PAGE_SIZE_4K) | flags;

    pd[pd_slot] = (uint64_t)(uintptr_t)pt | PAGE_PRESENT | PAGE_WRITABLE;
    return true;
}

static bool map_page_2m(uintptr_t virt, uintptr_t phys, bool writable, bool executable)
{
    uint64_t* pd = get_or_create_pd(virt);
    uint64_t flags = paging_default_leaf_flags(writable) | PAGE_PAGE_SIZE;

    if (pd == NULL)
        return false;
    if (!executable)
        flags |= PAGE_NX;

    pd[pd_index(virt)] = (uint64_t)phys | flags;
    return true;
}

static bool map_page_4k(uintptr_t virt, uintptr_t phys, bool writable, bool executable)
{
    uint64_t* pd = get_or_create_pd(virt);
    uint64_t* pt;
    uint64_t flags = paging_default_leaf_flags(writable);
    size_t slot;

    if (pd == NULL)
        return false;

    slot = pd_index(virt);
    if ((pd[slot] & PAGE_PAGE_SIZE) != 0u) {
        if (!split_large_page(pd, slot))
            return false;
    } else if ((pd[slot] & PAGE_PRESENT) == 0u) {
        uint64_t* new_pt = alloc_page_table();

        if (new_pt == NULL)
            return false;
        pd[slot] = (uint64_t)(uintptr_t)new_pt | PAGE_PRESENT | PAGE_WRITABLE;
    }

    pt = (uint64_t*)(uintptr_t)(pd[slot] & ~0xFFFull);
    if (!executable)
        flags |= PAGE_NX;
    pt[pt_index(virt)] = (uint64_t)phys | flags;
    return true;
}

static bool map_range_4k(uintptr_t start, uintptr_t end, bool writable, bool executable)
{
    uintptr_t cursor = align_down_page(start, PAGE_SIZE_4K);
    uintptr_t aligned_end = align_up_page(end, PAGE_SIZE_4K);

    while (cursor < aligned_end) {
        if (!map_page_4k(cursor, cursor, writable, executable))
            return false;
        cursor += PAGE_SIZE_4K;
    }
    return true;
}

static bool map_kernel_image(void)
{
    uintptr_t text_start = (uintptr_t)&__text_start;
    uintptr_t text_end = (uintptr_t)&__text_end;
    uintptr_t rodata_start = (uintptr_t)&__rodata_start;
    uintptr_t rodata_end = (uintptr_t)&__rodata_end;
    uintptr_t data_start = (uintptr_t)&__data_start;
    uintptr_t data_end = (uintptr_t)&__kernel_image_end;
    uintptr_t kernel_chunk_start = align_down_page((uintptr_t)&_start, PAGE_SIZE_2M);
    uintptr_t kernel_chunk_end = align_up_page((uintptr_t)&__kernel_image_end, PAGE_SIZE_2M);
    uintptr_t cursor;

    for (cursor = kernel_chunk_start; cursor < kernel_chunk_end; cursor += PAGE_SIZE_2M) {
        uint64_t* pd = get_or_create_pd(cursor);
        size_t slot;

        if (pd == NULL)
            return false;
        slot = pd_index(cursor);
        if ((pd[slot] & PAGE_PRESENT) == 0u)
            pd[slot] = (uint64_t)cursor | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PAGE_SIZE;
        if ((pd[slot] & PAGE_PAGE_SIZE) != 0u && !split_large_page(pd, slot))
            return false;
    }

    if (!map_range_4k(text_start, text_end, false, true))
        return false;
    if (!map_range_4k(rodata_start, rodata_end, false, false))
        return false;
    if (data_end > data_start && !map_range_4k(data_start, data_end, true, false))
        return false;

    return true;
}

static bool map_framebuffer(const BootInfo* boot_info)
{
    uintptr_t start;
    uintptr_t end;

    if (boot_info == NULL)
        return false;
    if ((boot_info->console_flags & BOOTINFO_CONSOLE_FRAMEBUFFER) == 0u)
        return true;
    if (boot_info->framebuffer_base == 0u)
        return true;

    start = boot_info->framebuffer_base;
    end = start + (uintptr_t)boot_info->framebuffer_pixels_per_scanline
        * (uintptr_t)boot_info->framebuffer_height * sizeof(uint32_t);
    return map_range_4k(start, end, true, false);
}

static void enable_nxe_and_write_protect(void)
{
    uint64_t efer;
    uint64_t cr0;
    uint32_t efer_low;
    uint32_t efer_high;

    __asm__ volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(EFER_MSR));
    efer = (uint64_t)efer_low | ((uint64_t)efer_high << 32u);
    efer |= EFER_NXE;
    efer_low = (uint32_t)efer;
    efer_high = (uint32_t)(efer >> 32u);
    __asm__ volatile("wrmsr" : : "c"(EFER_MSR), "a"(efer_low), "d"(efer_high));

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WRITE_PROTECT;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static void load_cr3(uintptr_t pml4)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

bool paging_init(const BootInfo* boot_info)
{
    uintptr_t cursor;

    g_pml4 = alloc_page_table();
    if (g_pml4 == NULL)
        return false;

    if (!map_range_4k(PAGE_SIZE_4K, PAGE_SIZE_2M, true, true))
        return false;

    for (cursor = PAGE_SIZE_2M; cursor < LOW_IDENTITY_LIMIT; cursor += PAGE_SIZE_2M) {
        if (!map_page_2m(cursor, cursor, true, true))
            return false;
    }

    if (!map_kernel_image())
        return false;
    if (!map_framebuffer(boot_info))
        return false;

    enable_nxe_and_write_protect();
    load_cr3((uintptr_t)g_pml4);
    return true;
}
