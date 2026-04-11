#include "memory.h"
#include "console.h"
#include "kstring.h"

#define PAGE_SIZE 4096u
#define MEMORY_MAX_FREE_RANGES 256u
#define MEMORY_HEAP_ALIGNMENT 16u
#define MEMORY_MAX_PHYS_ADDR 0x0000000100000000ull
#define MEMORY_RESERVED_LOW_END 0x0000000000100000ull
#define MEMORY_PROGRAM_REGION_START 0x0000000000500000ull
#define MEMORY_PROGRAM_REGION_END 0x0000000000600000ull
#define EFI_MEMORY_TYPE_CONVENTIONAL 7u

typedef struct EfiMemoryDescriptor {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EfiMemoryDescriptor;

typedef struct FreePageRange {
    uintptr_t base;
    size_t page_count;
} FreePageRange;

typedef struct HeapBlock {
    size_t size;
    bool free;
    struct HeapBlock* next;
} HeapBlock;

extern uint8_t _start;
extern uint8_t __kernel_image_end;

static FreePageRange g_free_ranges[MEMORY_MAX_FREE_RANGES];
static size_t g_free_range_count = 0;
static size_t g_total_pages = 0;
static HeapBlock* g_heap_head = NULL;
static bool g_memory_ready = false;

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void memory_write_decimal(size_t value)
{
    char digits[32];
    size_t count = 0;

    if (value == 0u) {
        console_write_char('0');
        return;
    }

    while (value != 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count != 0u)
        console_write_char(digits[--count]);
}

static bool append_free_range(uintptr_t base, size_t page_count)
{
    size_t insert_at;

    if (page_count == 0u)
        return true;

    if (g_free_range_count >= MEMORY_MAX_FREE_RANGES)
        return false;

    insert_at = 0u;
    while (insert_at < g_free_range_count && g_free_ranges[insert_at].base < base)
        ++insert_at;

    while (insert_at < g_free_range_count
        && base + page_count * PAGE_SIZE > g_free_ranges[insert_at].base)
        return false;

    if (insert_at > 0u) {
        FreePageRange* prev = &g_free_ranges[insert_at - 1u];

        if (prev->base + prev->page_count * PAGE_SIZE == base) {
            prev->page_count += page_count;
            if (insert_at < g_free_range_count
                && prev->base + prev->page_count * PAGE_SIZE == g_free_ranges[insert_at].base) {
                prev->page_count += g_free_ranges[insert_at].page_count;
                while (insert_at + 1u < g_free_range_count) {
                    g_free_ranges[insert_at] = g_free_ranges[insert_at + 1u];
                    ++insert_at;
                }
                --g_free_range_count;
            }
            return true;
        }
    }

    if (insert_at < g_free_range_count
        && base + page_count * PAGE_SIZE == g_free_ranges[insert_at].base) {
        g_free_ranges[insert_at].base = base;
        g_free_ranges[insert_at].page_count += page_count;
        return true;
    }

    {
        size_t index = g_free_range_count;

        while (index > insert_at) {
            g_free_ranges[index] = g_free_ranges[index - 1u];
            --index;
        }
        g_free_ranges[insert_at].base = base;
        g_free_ranges[insert_at].page_count = page_count;
        ++g_free_range_count;
    }

    return true;
}

static bool reserve_range(uintptr_t reserve_start, uintptr_t reserve_end)
{
    size_t index = 0u;

    reserve_start = align_down_uintptr(reserve_start, PAGE_SIZE);
    reserve_end = align_up_uintptr(reserve_end, PAGE_SIZE);
    if (reserve_end <= reserve_start)
        return true;

    while (index < g_free_range_count) {
        uintptr_t range_start = g_free_ranges[index].base;
        uintptr_t range_end = range_start + g_free_ranges[index].page_count * PAGE_SIZE;

        if (reserve_end <= range_start || reserve_start >= range_end) {
            ++index;
            continue;
        }

        if (reserve_start <= range_start && reserve_end >= range_end) {
            size_t move_index = index;

            while (move_index + 1u < g_free_range_count) {
                g_free_ranges[move_index] = g_free_ranges[move_index + 1u];
                ++move_index;
            }
            --g_free_range_count;
            continue;
        }

        if (reserve_start <= range_start) {
            g_free_ranges[index].base = reserve_end;
            g_free_ranges[index].page_count = (range_end - reserve_end) / PAGE_SIZE;
            ++index;
            continue;
        }

        if (reserve_end >= range_end) {
            g_free_ranges[index].page_count = (reserve_start - range_start) / PAGE_SIZE;
            ++index;
            continue;
        }

        if (g_free_range_count >= MEMORY_MAX_FREE_RANGES)
            return false;

        {
            uintptr_t right_start = reserve_end;
            size_t right_pages = (range_end - reserve_end) / PAGE_SIZE;
            size_t move_index = g_free_range_count;

            while (move_index > index + 1u) {
                g_free_ranges[move_index] = g_free_ranges[move_index - 1u];
                --move_index;
            }
            g_free_ranges[index + 1u].base = right_start;
            g_free_ranges[index + 1u].page_count = right_pages;
            ++g_free_range_count;
        }

        g_free_ranges[index].page_count = (reserve_start - range_start) / PAGE_SIZE;
        index += 2u;
    }

    return true;
}

static bool add_descriptor_range(uintptr_t start, uintptr_t end)
{
    uintptr_t clipped_start;
    uintptr_t clipped_end;

    if (start >= MEMORY_MAX_PHYS_ADDR)
        return true;

    clipped_start = align_up_uintptr(start, PAGE_SIZE);
    clipped_end = end > MEMORY_MAX_PHYS_ADDR ? MEMORY_MAX_PHYS_ADDR : end;
    clipped_end = align_down_uintptr(clipped_end, PAGE_SIZE);

    if (clipped_end <= clipped_start)
        return true;

    if (!append_free_range(clipped_start, (clipped_end - clipped_start) / PAGE_SIZE))
        return false;

    g_total_pages += (clipped_end - clipped_start) / PAGE_SIZE;
    return true;
}

static void heap_insert_block(HeapBlock** head, HeapBlock* block)
{
    HeapBlock* prev = NULL;
    HeapBlock* cursor = *head;

    while (cursor != NULL && cursor < block) {
        prev = cursor;
        cursor = cursor->next;
    }

    block->next = cursor;
    if (prev == NULL)
        *head = block;
    else
        prev->next = block;
}

static void heap_merge_adjacent(HeapBlock* head)
{
    HeapBlock* block = head;

    while (block != NULL && block->next != NULL) {
        uintptr_t block_end = (uintptr_t)(block + 1) + block->size;

        if (block->free
            && block->next->free
            && block_end == (uintptr_t)block->next) {
            block->size += sizeof(HeapBlock) + block->next->size;
            block->next = block->next->next;
            continue;
        }
        block = block->next;
    }
}

static void heap_init_region(HeapBlock** head, void* base, size_t bytes)
{
    HeapBlock* block = (HeapBlock*)base;

    block->size = bytes - sizeof(HeapBlock);
    block->free = true;
    block->next = NULL;
    *head = block;
}

static void* heap_alloc_from_list(HeapBlock** head, size_t aligned_size)
{
    HeapBlock* block = *head;

    while (block != NULL) {
        if (block->free && block->size >= aligned_size) {
            if (block->size >= aligned_size + sizeof(HeapBlock) + MEMORY_HEAP_ALIGNMENT) {
                HeapBlock* split = (HeapBlock*)((uint8_t*)(block + 1) + aligned_size);

                split->size = block->size - aligned_size - sizeof(HeapBlock);
                split->free = true;
                split->next = block->next;
                block->next = split;
                block->size = aligned_size;
            }

            block->free = false;
            return (void*)(block + 1);
        }
        block = block->next;
    }

    return NULL;
}

static void heap_free_to_list(HeapBlock* head, void* ptr)
{
    HeapBlock* block;

    if (ptr == NULL)
        return;

    block = ((HeapBlock*)ptr) - 1;
    block->free = true;
    heap_merge_adjacent(head);
}

static HeapBlock* heap_grow(size_t minimum_size)
{
    size_t required = align_up_size(sizeof(HeapBlock) + minimum_size, PAGE_SIZE);
    size_t pages = required / PAGE_SIZE;
    HeapBlock* block = (HeapBlock*)alloc_pages(pages);

    if (block == NULL)
        return NULL;

    block->size = pages * PAGE_SIZE - sizeof(HeapBlock);
    block->free = true;
    block->next = NULL;
    heap_insert_block(&g_heap_head, block);
    heap_merge_adjacent(g_heap_head);
    return block;
}

bool memory_init(const BootInfo* boot_info)
{
    uintptr_t cursor;

    if (boot_info == NULL)
        return false;
    if (boot_info->memory_map.base == 0u
        || boot_info->memory_map.size == 0u
        || boot_info->memory_map.descriptor_size < sizeof(EfiMemoryDescriptor))
        return false;

    g_free_range_count = 0u;
    g_total_pages = 0u;
    g_heap_head = NULL;
    g_memory_ready = false;

    for (cursor = boot_info->memory_map.base;
        cursor + boot_info->memory_map.descriptor_size <= boot_info->memory_map.base + boot_info->memory_map.size;
        cursor += boot_info->memory_map.descriptor_size) {
        const EfiMemoryDescriptor* descriptor = (const EfiMemoryDescriptor*)cursor;
        uintptr_t start;
        uintptr_t end;

        if (descriptor->type != EFI_MEMORY_TYPE_CONVENTIONAL)
            continue;
        if (descriptor->number_of_pages == 0u)
            continue;

        start = (uintptr_t)descriptor->physical_start;
        end = start + (uintptr_t)(descriptor->number_of_pages * PAGE_SIZE);
        if (!add_descriptor_range(start, end))
            return false;
    }

    if (!reserve_range(0u, MEMORY_RESERVED_LOW_END))
        return false;
    if (!reserve_range((uintptr_t)&_start, (uintptr_t)&__kernel_image_end))
        return false;
    if (!reserve_range(MEMORY_PROGRAM_REGION_START, MEMORY_PROGRAM_REGION_END))
        return false;
    if ((boot_info->console_flags & BOOTINFO_CONSOLE_FRAMEBUFFER) != 0u) {
        uintptr_t framebuffer_size = (uintptr_t)boot_info->framebuffer_pixels_per_scanline
            * (uintptr_t)boot_info->framebuffer_height * sizeof(uint32_t);

        if (!reserve_range(boot_info->framebuffer_base, boot_info->framebuffer_base + framebuffer_size))
            return false;
    }

    g_memory_ready = g_free_range_count != 0u;
    return g_memory_ready;
}

bool memory_self_test(void)
{
    size_t pages_before;
    size_t scratch_pages = 3u;
    void* scratch_region;
    HeapBlock* scratch_head;
    void* first_page;
    void* second_page;
    uint8_t* small;
    uint8_t* medium;
    uint8_t* large;
    uint8_t* reused_small;
    uint8_t* reused_large;

    if (!g_memory_ready)
        return false;

    pages_before = memory_free_pages();

    first_page = alloc_pages(1u);
    second_page = alloc_pages(2u);
    if (first_page == NULL || second_page == NULL)
        return false;
    ((volatile uint8_t*)first_page)[0] = 0x5Au;
    ((volatile uint8_t*)second_page)[PAGE_SIZE] = 0xA5u;
    free_pages(second_page, 2u);
    free_pages(first_page, 1u);
    if (memory_free_pages() != pages_before)
        return false;

    scratch_region = alloc_pages(scratch_pages);
    if (scratch_region == NULL)
        return false;

    heap_init_region(&scratch_head, scratch_region, scratch_pages * PAGE_SIZE);
    small = (uint8_t*)heap_alloc_from_list(&scratch_head, align_up_size(64u, MEMORY_HEAP_ALIGNMENT));
    medium = (uint8_t*)heap_alloc_from_list(&scratch_head, align_up_size(512u, MEMORY_HEAP_ALIGNMENT));
    large = (uint8_t*)heap_alloc_from_list(&scratch_head, align_up_size(6000u, MEMORY_HEAP_ALIGNMENT));
    if (small == NULL || medium == NULL || large == NULL)
        goto fail_scratch;

    k_memset(small, 0x11, 64u);
    k_memset(medium, 0x22, 512u);
    k_memset(large, 0x33, 6000u);
    heap_free_to_list(scratch_head, medium);
    heap_free_to_list(scratch_head, small);
    heap_free_to_list(scratch_head, large);

    reused_small = (uint8_t*)heap_alloc_from_list(&scratch_head, align_up_size(128u, MEMORY_HEAP_ALIGNMENT));
    reused_large = (uint8_t*)heap_alloc_from_list(&scratch_head, align_up_size(4096u, MEMORY_HEAP_ALIGNMENT));
    if (reused_small == NULL || reused_large == NULL)
        goto fail_scratch;

    heap_free_to_list(scratch_head, reused_large);
    heap_free_to_list(scratch_head, reused_small);
    free_pages(scratch_region, scratch_pages);
    return memory_free_pages() == pages_before;

fail_scratch:
    free_pages(scratch_region, scratch_pages);
    return false;
}

void* alloc_pages(size_t page_count)
{
    size_t index;

    if (!g_memory_ready || page_count == 0u)
        return NULL;

    for (index = 0u; index < g_free_range_count; ++index) {
        if (g_free_ranges[index].page_count >= page_count) {
            uintptr_t base = g_free_ranges[index].base;

            g_free_ranges[index].base += page_count * PAGE_SIZE;
            g_free_ranges[index].page_count -= page_count;
            if (g_free_ranges[index].page_count == 0u) {
                size_t move_index = index;

                while (move_index + 1u < g_free_range_count) {
                    g_free_ranges[move_index] = g_free_ranges[move_index + 1u];
                    ++move_index;
                }
                --g_free_range_count;
            }
            return (void*)base;
        }
    }

    return NULL;
}

void free_pages(void* ptr, size_t page_count)
{
    if (!g_memory_ready || ptr == NULL || page_count == 0u)
        return;

    append_free_range((uintptr_t)ptr, page_count);
}

void* kmalloc(size_t size)
{
    size_t aligned_size;
    void* allocation;

    if (!g_memory_ready || size == 0u)
        return NULL;

    aligned_size = align_up_size(size, MEMORY_HEAP_ALIGNMENT);
    allocation = heap_alloc_from_list(&g_heap_head, aligned_size);
    if (allocation != NULL)
        return allocation;

    if (heap_grow(aligned_size) == NULL)
        return NULL;
    return kmalloc(size);
}

void kfree(void* ptr)
{
    heap_free_to_list(g_heap_head, ptr);
}

size_t memory_free_pages(void)
{
    size_t total = 0u;
    size_t index;

    for (index = 0u; index < g_free_range_count; ++index)
        total += g_free_ranges[index].page_count;

    return total;
}

size_t memory_total_pages(void)
{
    return g_total_pages;
}

void memory_dump_stats(void)
{
    console_write("memory: total pages=");
    memory_write_decimal(memory_total_pages());
    console_write(" free pages=");
    memory_write_decimal(memory_free_pages());
    console_write(" free bytes=");
    memory_write_decimal(memory_free_pages() * PAGE_SIZE);
    console_write_char('\n');
}
