#include "process.h"
#include "console.h"
#include "fs.h"
#include "keyboard.h"
#include "kstring.h"
#include "memory.h"
#include "paging.h"
#include "process_layout.h"

#define PAGE_SIZE 4096u
#define PROCESS_MAX_FILE_SIZE 131072u
#define PROCESS_MAX_ARGS 8u
#define PROCESS_ARG_BUFFER_SIZE 256u
#define PT_LOAD 1u
#define ELF_MAGIC 0x464C457Fu
#define ELFCLASS64 2u
#define EM_X86_64 62u

typedef struct __attribute__((packed)) Elf64Header {
    uint32_t magic;
    uint8_t ident_class;
    uint8_t ident_data;
    uint8_t ident_version;
    uint8_t ident_osabi;
    uint8_t ident_abiversion;
    uint8_t ident_pad[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} Elf64Header;

typedef struct __attribute__((packed)) Elf64ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} Elf64ProgramHeader;

typedef struct ProcessLoadContext {
    uint8_t* dst;
    uint32_t capacity;
    uint32_t size;
} ProcessLoadContext;

typedef struct ProcessUserContext {
    ProgramInfo info;
    uintptr_t argv[PROCESS_MAX_ARGS];
    char cwd[PATH_CAPACITY];
    char arg_buffer[PROCESS_ARG_BUFFER_SIZE];
} ProcessUserContext;

extern uint64_t program_invoke(uintptr_t entry_point, uintptr_t info_ptr, uintptr_t stack_top);
extern uint8_t __kernel_image_end;

static Process g_foreground_process;
static Process* g_active_process = NULL;

static size_t bytes_to_pages(size_t size)
{
    return (size + PAGE_SIZE - 1u) / PAGE_SIZE;
}

static uintptr_t align_down_page(uintptr_t value)
{
    return value & ~((uintptr_t)PAGE_SIZE - 1u);
}

static uintptr_t align_up_page(uintptr_t value)
{
    return (value + (uintptr_t)PAGE_SIZE - 1u) & ~((uintptr_t)PAGE_SIZE - 1u);
}

static void process_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0;

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

static bool process_file_chunk_copy(const uint8_t* data, uint32_t length, void* context)
{
    ProcessLoadContext* load_context = (ProcessLoadContext*)context;

    if (load_context->size + length > load_context->capacity)
        return false;

    k_memcpy(load_context->dst + load_context->size, data, length);
    load_context->size += length;
    return true;
}

static bool process_prepare_address_space(Process* process);

static bool process_range_contains(uintptr_t base, size_t size, uintptr_t ptr, size_t length)
{
    uintptr_t offset;

    if (length == 0u)
        return true;
    if (ptr < base)
        return false;

    offset = ptr - base;
    if (offset > size)
        return false;
    return length <= size - offset;
}

static bool process_user_range_valid(const Process* process, uintptr_t ptr, size_t length)
{
    if (process == NULL)
        return false;

    return process_range_contains(process->address_space.image_base, process->address_space.image_capacity, ptr, length)
        || process_range_contains(process->address_space.stack_base, process->address_space.stack_size, ptr, length);
}

static bool process_stage_user_context(Process* process)
{
    ProcessUserContext* user_context;
    size_t index;

    if (process == NULL)
        return false;
    if (sizeof(ProcessUserContext) > process->address_space.stack_size)
        return false;

    user_context = (ProcessUserContext*)(uintptr_t)process->address_space.stack_base;
    k_memset(user_context, 0, sizeof(*user_context));
    k_memcpy(user_context->cwd, process->runtime.cwd, sizeof(user_context->cwd));
    k_memcpy(user_context->arg_buffer, process->runtime.arg_buffer, sizeof(user_context->arg_buffer));

    for (index = 0u; index < process->runtime.info.argc; ++index) {
        uintptr_t source_ptr = process->runtime.argv[index];
        uintptr_t source_base = (uintptr_t)process->runtime.arg_buffer;
        size_t offset;

        if (source_ptr < source_base)
            return false;
        offset = (size_t)(source_ptr - source_base);
        if (offset >= sizeof(user_context->arg_buffer))
            return false;
        user_context->argv[index] = (uintptr_t)(user_context->arg_buffer + offset);
    }

    user_context->info.magic = PROGRAM_INFO_MAGIC;
    user_context->info.reserved = 0u;
    user_context->info.argc = process->runtime.info.argc;
    user_context->info.argv_addr = (uintptr_t)user_context->argv;
    user_context->info.cwd_addr = (uintptr_t)user_context->cwd;
    process->runtime.user_info_addr = (uintptr_t)&user_context->info;
    return true;
}

static void process_finish_exit(Process* process, uint32_t status)
{
    if (process == NULL)
        return;

    process->runtime.exit_requested = true;
    process->runtime.exit_status = status;
    process->state = PROCESS_STATE_EXITED;
}

static void process_reset_fields(Process* process)
{
    k_memset(process, 0, sizeof(*process));
    process->state = PROCESS_STATE_IDLE;
    process->address_space.stack_size = PROCESS_ABI_STACK_SIZE;
    process->address_space.stack_base = PROCESS_ABI_STACK_BASE;
    process->address_space.stack_top = PROCESS_ABI_STACK_TOP;
    process->address_space.stack_page_count = bytes_to_pages(PROCESS_ABI_STACK_SIZE);
    process->address_space.file_capacity = PROCESS_MAX_FILE_SIZE;
    process->address_space.file_page_count = bytes_to_pages(PROCESS_MAX_FILE_SIZE);
}

static void process_prepare_runtime(Process* process)
{
    process->runtime.info.magic = PROGRAM_INFO_MAGIC;
    process->runtime.info.reserved = 0u;
    process->runtime.user_info_addr = 0u;
    process->runtime.exit_status = 0u;
    process->runtime.exit_requested = false;
}

static bool process_tokenize_args(Process* process, const char* command, const char* argument_line)
{
    const char* cursor;
    size_t argc = 0;
    uint32_t offset = 0;

    k_memset(process->runtime.arg_buffer, 0, sizeof(process->runtime.arg_buffer));
    k_memset(process->runtime.argv, 0, sizeof(process->runtime.argv));

    if (command == NULL || *command == '\0')
        return false;

    cursor = command;
    while (*cursor != '\0') {
        if (offset + 1u >= sizeof(process->runtime.arg_buffer))
            return false;
        process->runtime.arg_buffer[offset++] = *cursor++;
    }
    process->runtime.arg_buffer[offset++] = '\0';
    process->runtime.argv[argc++] = (uintptr_t)process->runtime.arg_buffer;

    cursor = argument_line;
    if (cursor == NULL)
        cursor = "";

    while (*cursor != '\0') {
        uint32_t arg_start;

        while (*cursor != '\0' && k_is_space(*cursor))
            ++cursor;
        if (*cursor == '\0')
            break;
        if (argc >= PROCESS_MAX_ARGS)
            return false;

        arg_start = offset;
        while (*cursor != '\0' && !k_is_space(*cursor)) {
            if (offset + 1u >= sizeof(process->runtime.arg_buffer))
                return false;
            process->runtime.arg_buffer[offset++] = *cursor++;
        }
        process->runtime.arg_buffer[offset++] = '\0';
        process->runtime.argv[argc++] = (uintptr_t)(process->runtime.arg_buffer + arg_start);
    }

    process->runtime.info.argc = argc;
    process->runtime.info.argv_addr = (uintptr_t)process->runtime.argv;
    process->runtime.info.cwd_addr = (uintptr_t)process->runtime.cwd;
    return true;
}

static bool process_configure_image_layout(Process* process, uint32_t file_size)
{
    const Elf64Header* header = (const Elf64Header*)process->image.file_buffer;
    uint16_t index;
    uintptr_t image_min = ~(uintptr_t)0u;
    uintptr_t image_end = 0u;
    bool has_load_segment = false;

    if (file_size < sizeof(*header)) {
        console_write_line("invalid program");
        return false;
    }
    if (header->magic != ELF_MAGIC
        || header->ident_class != ELFCLASS64
        || header->machine != EM_X86_64
        || header->phentsize != sizeof(Elf64ProgramHeader)
        || header->phoff > file_size
        || (uint64_t)header->phnum * header->phentsize > file_size - header->phoff) {
        console_write_line("invalid program");
        return false;
    }

    for (index = 0; index < header->phnum; ++index) {
        const Elf64ProgramHeader* segment = (const Elf64ProgramHeader*)(process->image.file_buffer + header->phoff + (uint64_t)index * header->phentsize);
        if (segment->type != PT_LOAD)
            continue;
        if (segment->offset > file_size || segment->filesz > file_size - segment->offset || segment->filesz > segment->memsz) {
            console_write_line("invalid program");
            return false;
        }
        uintptr_t segment_start;
        uintptr_t segment_end;

        if (segment->vaddr < PROCESS_ABI_IMAGE_BASE
            || segment->vaddr >= PROCESS_ABI_IMAGE_LIMIT
            || segment->memsz > PROCESS_ABI_IMAGE_LIMIT - segment->vaddr) {
            console_write_line("program too large");
            return false;
        }

        has_load_segment = true;
        segment_start = align_down_page((uintptr_t)segment->vaddr);
        segment_end = align_up_page((uintptr_t)segment->vaddr + (uintptr_t)segment->memsz);
        if (segment_start < image_min)
            image_min = segment_start;
        if (segment_end > image_end)
            image_end = segment_end;
    }

    if (!has_load_segment) {
        console_write_line("invalid program");
        return false;
    }
    if (image_min < (uintptr_t)&__kernel_image_end) {
        console_write_line("program image overlaps kernel");
        return false;
    }
    if (header->entry < image_min || header->entry >= image_end) {
        console_write_line("invalid program");
        return false;
    }
    if (image_end <= image_min) {
        console_write_line("invalid program");
        return false;
    }

    process->address_space.image_base = image_min;
    process->address_space.image_capacity = image_end - image_min;
    process->address_space.image_page_count = bytes_to_pages(process->address_space.image_capacity);
    if (image_min < PROCESS_ABI_IMAGE_BASE || image_end > PROCESS_ABI_IMAGE_LIMIT) {
        console_write_line("program too large");
        return false;
    }
    if (image_end + PROCESS_ABI_STACK_GAP > process->address_space.stack_base) {
        console_write_line("program too large");
        return false;
    }

    process->image.entry_point = (uintptr_t)header->entry;
    process->image.file_size = file_size;
    return true;
}

static bool process_copy_image_segments(Process* process, uint32_t file_size)
{
    const Elf64Header* header = (const Elf64Header*)process->image.file_buffer;
    uint16_t index;

    k_memset((void*)process->address_space.image_base, 0, process->address_space.image_capacity);
    for (index = 0; index < header->phnum; ++index) {
        const Elf64ProgramHeader* segment = (const Elf64ProgramHeader*)(process->image.file_buffer + header->phoff + (uint64_t)index * header->phentsize);

        if (segment->type != PT_LOAD)
            continue;
        if (segment->offset > file_size || segment->filesz > file_size - segment->offset || segment->filesz > segment->memsz) {
            console_write_line("invalid program");
            return false;
        }

        k_memset((void*)(uintptr_t)segment->vaddr, 0, (size_t)segment->memsz);
        k_memcpy((void*)(uintptr_t)segment->vaddr, process->image.file_buffer + segment->offset, (size_t)segment->filesz);
    }

    process->state = PROCESS_STATE_LOADED;
    return true;
}

static bool process_load_binary(Process* process, const char* path)
{
    FsNodeInfo node;
    ProcessLoadContext load_context;
    uintptr_t previous_root;

    if (fs_stat(path, &node) != FS_OK)
        return false;
    if (node.type != FS_NODE_FILE) {
        console_write_line("program path is not a file");
        return false;
    }
    if (node.size > process->address_space.file_capacity) {
        console_write_line("program too large");
        return false;
    }

    load_context.dst = process->image.file_buffer;
    load_context.capacity = (uint32_t)process->address_space.file_capacity;
    load_context.size = 0;
    k_memset(load_context.dst, 0, process->address_space.file_capacity);

    if (fs_read_file(path, process_file_chunk_copy, &load_context) != FS_OK) {
        console_write_line("program read failed");
        return false;
    }
    if (load_context.size != node.size) {
        console_write_line("program size mismatch");
        return false;
    }

    if (!process_configure_image_layout(process, load_context.size))
        return false;
    if (!process_prepare_address_space(process)) {
        console_write_line("program address space setup failed");
        return false;
    }

    previous_root = paging_current_root();
    paging_activate_root(process->address_space.page_table_root);
    if (!process_stage_user_context(process)) {
        console_write_line("program user context setup failed");
        paging_activate_root(previous_root);
        return false;
    }
    if (!process_copy_image_segments(process, load_context.size)) {
        console_write_line("program image copy failed");
        paging_activate_root(previous_root);
        return false;
    }
    paging_activate_root(previous_root);
    return true;
}

static bool process_prepare_address_space(Process* process)
{
    if (process == NULL)
        return false;

    process->address_space.page_table_root = paging_create_address_space();
    if (process->address_space.page_table_root == 0u) {
        console_write_line("program: page table alloc failed");
        return false;
    }

    process->address_space.image_backing = alloc_pages(process->address_space.image_page_count);
    if (process->address_space.image_backing == NULL) {
        console_write_line("program: image backing alloc failed");
        goto fail;
    }

    process->address_space.stack_backing = alloc_pages(process->address_space.stack_page_count);
    if (process->address_space.stack_backing == NULL) {
        console_write_line("program: stack backing alloc failed");
        goto fail;
    }

    if (!paging_map_user_range(
            process->address_space.page_table_root,
            process->address_space.image_base,
            (uintptr_t)process->address_space.image_backing,
            process->address_space.image_capacity,
            true,
            true)) {
        console_write_line("program: image map failed");
        goto fail;
    }

    if (!paging_map_user_range(
            process->address_space.page_table_root,
            process->address_space.stack_base,
            (uintptr_t)process->address_space.stack_backing,
            process->address_space.stack_size,
            true,
            false)) {
        console_write_line("program: stack map failed");
        goto fail;
    }
    return true;

fail:
    if (process->address_space.stack_backing != NULL) {
        free_pages(process->address_space.stack_backing, process->address_space.stack_page_count);
        process->address_space.stack_backing = NULL;
    }
    if (process->address_space.image_backing != NULL) {
        free_pages(process->address_space.image_backing, process->address_space.image_page_count);
        process->address_space.image_backing = NULL;
    }
    if (process->address_space.page_table_root != 0u) {
        paging_destroy_address_space(process->address_space.page_table_root);
        process->address_space.page_table_root = 0u;
    }
    return false;
}

static void process_release_address_space(Process* process)
{
    if (process == NULL)
        return;

    if (process->address_space.image_backing != NULL) {
        free_pages(process->address_space.image_backing, process->address_space.image_page_count);
        process->address_space.image_backing = NULL;
    }

    if (process->address_space.stack_backing != NULL) {
        free_pages(process->address_space.stack_backing, process->address_space.stack_page_count);
        process->address_space.stack_backing = NULL;
    }

    if (process->address_space.file_buffer != NULL) {
        free_pages(process->address_space.file_buffer, process->address_space.file_page_count);
        process->address_space.file_buffer = NULL;
        process->image.file_buffer = NULL;
    }

    if (process->address_space.page_table_root != 0u) {
        paging_destroy_address_space(process->address_space.page_table_root);
        process->address_space.page_table_root = 0u;
    }

    process->runtime.user_info_addr = 0u;
}

void process_init(Process* process)
{
    if (process == NULL)
        return;

    process_reset_fields(process);
    process_prepare_runtime(process);
}

bool process_run_foreground(Process* process, const char* path, const char* command, const char* argument_line, const char* cwd)
{
    uint64_t invoke_result;
    uintptr_t kernel_root;

    if (process == NULL || path == NULL || command == NULL || cwd == NULL)
        return false;

    process_init(process);
    if (!process_tokenize_args(process, command, argument_line)) {
        console_write_line("program args invalid");
        return false;
    }

    process->address_space.file_buffer = alloc_pages(process->address_space.file_page_count);
    if (process->address_space.file_buffer == NULL) {
        console_write_line("program file buffer alloc failed");
        process_release_address_space(process);
        return false;
    }
    process->image.file_buffer = (uint8_t*)process->address_space.file_buffer;

    k_strcpy(process->runtime.cwd, cwd);
    if (!process_load_binary(process, path)) {
        process_release_address_space(process);
        return false;
    }

    process->runtime.exit_requested = false;
    process->runtime.exit_status = 0u;
    process->state = PROCESS_STATE_RUNNING;
    g_active_process = process;
    kernel_root = paging_current_root();
    paging_activate_root(process->address_space.page_table_root);
    invoke_result = program_invoke(
        process->image.entry_point,
        process->runtime.user_info_addr,
        process->address_space.stack_top);
    paging_activate_root(kernel_root);
    g_active_process = NULL;

    if (invoke_result == 0u) {
        console_write_line("program returned unexpectedly");
        process_release_address_space(process);
        return false;
    }
    if (!process->runtime.exit_requested) {
        console_write_line("program returned unexpectedly");
        process_release_address_space(process);
        return false;
    }
    if (process->runtime.exit_status != 0u) {
        console_write("program exited with status ");
        process_write_u32(process->runtime.exit_status);
        console_write_char('\n');
        process_release_address_space(process);
        return false;
    }

    process_release_address_space(process);
    return true;
}

bool process_handle_syscall(TrapFrame* frame)
{
    Process* process = g_active_process;

    if (frame == NULL || process == NULL)
        return false;

    switch ((uint32_t)frame->rax) {
    case PROGRAM_SYSCALL_EXIT:
        process_finish_exit(process, (uint32_t)frame->rdi);
        frame->rax = 1u;
        return true;
    case PROGRAM_SYSCALL_WRITE:
        if (!process_user_range_valid(process, (uintptr_t)frame->rdi, (size_t)frame->rsi)) {
            frame->rax = 0u;
            return true;
        }
        console_write_n((const char*)(uintptr_t)frame->rdi, (size_t)frame->rsi);
        frame->rax = frame->rsi;
        return true;
    case PROGRAM_SYSCALL_READ_LINE:
        if (frame->rsi == 0u) {
            frame->rax = 0u;
            return true;
        }
        if (!process_user_range_valid(process, (uintptr_t)frame->rdi, (size_t)frame->rsi)) {
            frame->rax = 0u;
            return true;
        }
        frame->rax = keyboard_read_line((char*)(uintptr_t)frame->rdi, (size_t)frame->rsi);
        return true;
    default:
        frame->rax = 0u;
        return true;
    }
}

bool process_should_return_to_kernel(void)
{
    return g_active_process != NULL && g_active_process->runtime.exit_requested;
}

bool process_handle_fault(TrapFrame* frame)
{
    Process* process = g_active_process;

    if (frame == NULL || process == NULL)
        return false;
    if ((frame->cs & 0x3u) != 0x3u)
        return false;

    process_finish_exit(process, 0xFFFFFFFFu);
    return true;
}

Process* process_foreground(void)
{
    return &g_foreground_process;
}
