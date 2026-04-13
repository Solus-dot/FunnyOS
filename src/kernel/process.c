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

extern uint64_t program_invoke(uintptr_t entry_point, uintptr_t info_ptr, uintptr_t stack_top);
extern void program_exit_resume(void);
extern uint8_t __kernel_image_end;

static Process g_foreground_process;
static Process* g_active_process = NULL;

static size_t bytes_to_pages(size_t size)
{
    return (size + PAGE_SIZE - 1u) / PAGE_SIZE;
}

static uintptr_t align_down_page(uintptr_t value)
{
    return value & ~(PAGE_SIZE - 1u);
}

static uintptr_t align_up_page(uintptr_t value)
{
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
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

static void process_finish_exit(Process* process, uint32_t status, TrapFrame* frame)
{
    if (process == NULL || frame == NULL)
        return;

    process->runtime.exit_requested = true;
    process->runtime.exit_status = status;
    process->state = PROCESS_STATE_EXITED;
    frame->rax = 1u;
    frame->rip = (uintptr_t)&program_exit_resume;
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
        console_write_line("program load failed");
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

    if (fs_stat(path, &node) != FS_OK)
        return false;
    if (node.type != FS_NODE_FILE) {
        console_write_line("program load failed");
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
        console_write_line("program load failed");
        return false;
    }
    if (load_context.size != node.size) {
        console_write_line("program load failed");
        return false;
    }

    if (!process_configure_image_layout(process, load_context.size))
        return false;
    if (!process_prepare_address_space(process))
        return false;
    return process_copy_image_segments(process, load_context.size);
}

static bool process_prepare_address_space(Process* process)
{
    process->address_space.image_backing = alloc_pages(process->address_space.image_page_count);
    if (process->address_space.image_backing == NULL)
        return false;

    process->address_space.stack_backing = alloc_pages(process->address_space.stack_page_count);
    if (process->address_space.stack_backing == NULL)
        return false;

    if (!paging_map_range(
            process->address_space.image_base,
            (uintptr_t)process->address_space.image_backing,
            process->address_space.image_capacity,
            true,
            true))
        return false;

    if (!paging_map_range(
            process->address_space.stack_base,
            (uintptr_t)process->address_space.stack_backing,
            process->address_space.stack_size,
            true,
            false))
        return false;

    k_memset((void*)process->address_space.image_base, 0, process->address_space.image_capacity);
    k_memset((void*)process->address_space.stack_base, 0, process->address_space.stack_size);
    return true;
}

static void process_release_address_space(Process* process)
{
    if (process == NULL)
        return;

    if (process->address_space.image_backing != NULL) {
        (void)paging_map_range(
            process->address_space.image_base,
            process->address_space.image_base,
            process->address_space.image_capacity,
            true,
            true);
        free_pages(process->address_space.image_backing, process->address_space.image_page_count);
        process->address_space.image_backing = NULL;
    }

    if (process->address_space.stack_backing != NULL) {
        (void)paging_map_range(
            process->address_space.stack_base,
            process->address_space.stack_base,
            process->address_space.stack_size,
            true,
            true);
        free_pages(process->address_space.stack_backing, process->address_space.stack_page_count);
        process->address_space.stack_backing = NULL;
    }

    if (process->address_space.file_buffer != NULL) {
        free_pages(process->address_space.file_buffer, process->address_space.file_page_count);
        process->address_space.file_buffer = NULL;
        process->image.file_buffer = NULL;
    }
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

    if (process == NULL || path == NULL || command == NULL || cwd == NULL)
        return false;

    process_init(process);
    if (!process_tokenize_args(process, command, argument_line)) {
        console_write_line("program load failed");
        return false;
    }

    process->address_space.file_buffer = alloc_pages(process->address_space.file_page_count);
    if (process->address_space.file_buffer == NULL) {
        console_write_line("program load failed");
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
    invoke_result = program_invoke(
        process->image.entry_point,
        (uintptr_t)&process->runtime.info,
        process->address_space.stack_top);
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
        process_finish_exit(process, (uint32_t)frame->rdi, frame);
        return true;
    case PROGRAM_SYSCALL_WRITE:
        console_write_n((const char*)(uintptr_t)frame->rdi, (size_t)frame->rsi);
        frame->rax = frame->rsi;
        return true;
    case PROGRAM_SYSCALL_READ_LINE:
        if (frame->rsi == 0u) {
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

Process* process_foreground(void)
{
    return &g_foreground_process;
}
