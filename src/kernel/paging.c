#include "paging.h"
#include "kstring.h"
#include "memory.h"

#define PAGE_SIZE_4K 0x1000ull
#define PAGE_SIZE_2M 0x200000ull
#define PAGE_TABLE_ENTRIES 512u
#define PAGE_PRESENT 0x001ull
#define PAGE_WRITABLE 0x002ull
#define PAGE_USER 0x004ull
#define PAGE_PAGE_SIZE 0x080ull
#define PAGE_NX (1ull << 63)
#define PAGE_TABLE_ADDR_MASK (~0xFFFull)
#define PAGE_2M_ADDR_MASK (~0x1FFFFFull)
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

static uint64_t* alloc_page_table(void)
{
    uint64_t* table = (uint64_t*)alloc_pages(1u);

    if (table == NULL)
        return NULL;
    k_memset(table, 0, PAGE_SIZE_4K);
    return table;
}

static uint64_t paging_default_leaf_flags(bool writable, bool user_accessible)
{
    return PAGE_PRESENT
        | (writable ? PAGE_WRITABLE : 0u)
        | (user_accessible ? PAGE_USER : 0u);
}

static uint64_t* get_or_create_table(uint64_t* parent, size_t index, bool user_accessible)
{
    uint64_t entry;
    uint64_t* table;
    uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE | (user_accessible ? PAGE_USER : 0u);

    if (parent == NULL)
        return NULL;

    entry = parent[index];
    if ((entry & PAGE_PRESENT) != 0u) {
        if ((entry & PAGE_PAGE_SIZE) != 0u)
            return NULL;
        if (user_accessible && (entry & PAGE_USER) == 0u)
            return NULL;
        return (uint64_t*)(uintptr_t)(entry & PAGE_TABLE_ADDR_MASK);
    }

    table = alloc_page_table();
    if (table == NULL)
        return NULL;
    parent[index] = (uint64_t)(uintptr_t)table | flags;
    return table;
}

static uint64_t* get_or_create_pd(uint64_t* root, uintptr_t virt, bool user_accessible)
{
    uint64_t* pdpt = get_or_create_table(root, pml4_index(virt), user_accessible);

    if (pdpt == NULL)
        return NULL;
    return get_or_create_table(pdpt, pdpt_index(virt), user_accessible);
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

    base = (uintptr_t)(entry & PAGE_2M_ADDR_MASK);
    flags = entry & (PAGE_PRESENT | PAGE_WRITABLE | PAGE_NX | PAGE_USER);
    for (index = 0u; index < PAGE_TABLE_ENTRIES; ++index)
        pt[index] = (uint64_t)(base + index * PAGE_SIZE_4K) | flags;

    pd[pd_slot] = (uint64_t)(uintptr_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    return true;
}

static bool map_page_2m(
    uint64_t* root,
    uintptr_t virt,
    uintptr_t phys,
    bool writable,
    bool executable,
    bool user_accessible)
{
    uint64_t* pd = get_or_create_pd(root, virt, user_accessible);
    uint64_t flags = paging_default_leaf_flags(writable, user_accessible) | PAGE_PAGE_SIZE;
    size_t slot;

    if (pd == NULL)
        return false;
    if (!executable)
        flags |= PAGE_NX;

    slot = pd_index(virt);
    pd[slot] = (uint64_t)phys | flags;
    return true;
}

static bool map_page_4k(
    uint64_t* root,
    uintptr_t virt,
    uintptr_t phys,
    bool writable,
    bool executable,
    bool user_accessible)
{
    uint64_t* pd = get_or_create_pd(root, virt, user_accessible);
    uint64_t* pt;
    uint64_t flags = paging_default_leaf_flags(writable, user_accessible);
    size_t slot;

    if (pd == NULL)
        return false;

    slot = pd_index(virt);
    if ((pd[slot] & PAGE_PAGE_SIZE) != 0u) {
        if (!split_large_page(pd, slot))
            return false;
    } else if ((pd[slot] & PAGE_PRESENT) == 0u) {
        uint64_t* new_pt = alloc_page_table();
        uint64_t table_flags = PAGE_PRESENT | PAGE_WRITABLE | (user_accessible ? PAGE_USER : 0u);

        if (new_pt == NULL)
            return false;
        pd[slot] = (uint64_t)(uintptr_t)new_pt | table_flags;
    } else if (user_accessible && (pd[slot] & PAGE_USER) == 0u) {
        return false;
    }

    pt = (uint64_t*)(uintptr_t)(pd[slot] & PAGE_TABLE_ADDR_MASK);
    if (!executable)
        flags |= PAGE_NX;
    pt[pt_index(virt)] = (uint64_t)phys | flags;
    return true;
}

static bool map_range_in_root(
    uint64_t* root,
    uintptr_t virt_start,
    uintptr_t phys_start,
    size_t size,
    bool writable,
    bool executable,
    bool user_accessible)
{
    uintptr_t virt_cursor;
    uintptr_t phys_cursor;
    uintptr_t virt_end;

    if (root == NULL || size == 0u)
        return false;
    if (virt_start + size < virt_start || phys_start + size < phys_start)
        return false;

    virt_cursor = align_down_page(virt_start, PAGE_SIZE_4K);
    phys_cursor = align_down_page(phys_start, PAGE_SIZE_4K);
    virt_end = align_up_page(virt_start + size, PAGE_SIZE_4K);

    while (virt_cursor < virt_end) {
        if (!map_page_4k(root, virt_cursor, phys_cursor, writable, executable, user_accessible))
            return false;
        virt_cursor += PAGE_SIZE_4K;
        phys_cursor += PAGE_SIZE_4K;
    }

    return true;
}

static bool map_range_identity_kernel(uintptr_t start, uintptr_t end, bool writable, bool executable)
{
    return map_range_in_root(g_pml4, start, start, end - start, writable, executable, false);
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
        uint64_t* pd = get_or_create_pd(g_pml4, cursor, false);
        size_t slot;

        if (pd == NULL)
            return false;
        slot = pd_index(cursor);
        if ((pd[slot] & PAGE_PRESENT) == 0u)
            pd[slot] = (uint64_t)cursor | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PAGE_SIZE;
        if ((pd[slot] & PAGE_PAGE_SIZE) != 0u && !split_large_page(pd, slot))
            return false;
    }

    if (!map_range_identity_kernel(text_start, text_end, false, true))
        return false;
    if (!map_range_identity_kernel(rodata_start, rodata_end, false, false))
        return false;
    if (data_end > data_start && !map_range_identity_kernel(data_start, data_end, true, false))
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
    return map_range_in_root(g_pml4, start, start, end - start, true, false, false);
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

static uintptr_t read_cr3(void)
{
    uintptr_t value;

    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void load_cr3(uintptr_t pml4)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

static void destroy_table_tree(uint64_t* table, uint32_t level)
{
    size_t i;

    if (table == NULL || level == 0u)
        return;

    for (i = 0u; i < PAGE_TABLE_ENTRIES; ++i) {
        uint64_t entry = table[i];

        if ((entry & PAGE_PRESENT) == 0u)
            continue;
        if (level == 1u || (entry & PAGE_PAGE_SIZE) != 0u)
            continue;

        destroy_table_tree((uint64_t*)(uintptr_t)(entry & PAGE_TABLE_ADDR_MASK), level - 1u);
        free_pages((void*)(uintptr_t)(entry & PAGE_TABLE_ADDR_MASK), 1u);
    }
}

bool paging_init(const BootInfo* boot_info)
{
    uintptr_t cursor;

    g_pml4 = alloc_page_table();
    if (g_pml4 == NULL)
        return false;

    if (!map_range_in_root(g_pml4, PAGE_SIZE_4K, PAGE_SIZE_4K, PAGE_SIZE_2M - PAGE_SIZE_4K, true, true, false))
        return false;

    for (cursor = PAGE_SIZE_2M; cursor < LOW_IDENTITY_LIMIT; cursor += PAGE_SIZE_2M) {
        if (!map_page_2m(g_pml4, cursor, cursor, true, true, false))
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

bool paging_map_range(uintptr_t virt_start, uintptr_t phys_start, size_t size, bool writable, bool executable)
{
    if (g_pml4 == NULL)
        return false;
    if (!map_range_in_root(g_pml4, virt_start, phys_start, size, writable, executable, false))
        return false;
    if (paging_current_root() == (uintptr_t)g_pml4)
        load_cr3((uintptr_t)g_pml4);
    return true;
}

uintptr_t paging_kernel_root(void)
{
    return (uintptr_t)g_pml4;
}

uintptr_t paging_current_root(void)
{
    return read_cr3() & PAGE_TABLE_ADDR_MASK;
}

void paging_activate_root(uintptr_t root)
{
    root &= PAGE_TABLE_ADDR_MASK;
    if (root != 0u)
        load_cr3(root);
}

uintptr_t paging_create_address_space(void)
{
    uint64_t* root;
    size_t i;

    if (g_pml4 == NULL)
        return 0u;

    root = alloc_page_table();
    if (root == NULL)
        return 0u;

    for (i = 0u; i < PAGE_TABLE_ENTRIES; ++i)
        root[i] = g_pml4[i];
    return (uintptr_t)root;
}

void paging_destroy_address_space(uintptr_t root_addr)
{
    uint64_t* root = (uint64_t*)root_addr;
    size_t i;

    if (root == NULL || root == g_pml4)
        return;

    for (i = 0u; i < PAGE_TABLE_ENTRIES; ++i) {
        uint64_t entry = root[i];

        if ((entry & PAGE_PRESENT) == 0u)
            continue;
        if (g_pml4 != NULL && entry == g_pml4[i])
            continue;
        if ((entry & PAGE_PAGE_SIZE) != 0u)
            continue;

        destroy_table_tree((uint64_t*)(uintptr_t)(entry & PAGE_TABLE_ADDR_MASK), 3u);
        free_pages((void*)(uintptr_t)(entry & PAGE_TABLE_ADDR_MASK), 1u);
    }

    free_pages(root, 1u);
}

bool paging_map_user_range(uintptr_t root, uintptr_t virt_start, uintptr_t phys_start, size_t size, bool writable, bool executable)
{
    uintptr_t root_addr = root & PAGE_TABLE_ADDR_MASK;

    if (root_addr == 0u)
        return false;
    if (!map_range_in_root((uint64_t*)root_addr, virt_start, phys_start, size, writable, executable, true))
        return false;
    if (paging_current_root() == root_addr)
        load_cr3(root_addr);
    return true;
}
