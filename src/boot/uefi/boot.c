#include "../../common/bootinfo.h"
#include "efi.h"

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

typedef void (__attribute__((sysv_abi)) *KernelEntry)(const BootInfo*);

EFI_GUID gEfiLoadedImageProtocolGuid = {0x5B1B31A1u, 0x9562u, 0x11d2u, {0x8Eu, 0x3Fu, 0x00u, 0xA0u, 0xC9u, 0x69u, 0x72u, 0x3Bu}};
EFI_GUID gEfiSimpleFileSystemProtocolGuid = {0x964E5B22u, 0x6459u, 0x11d2u, {0x8Eu, 0x39u, 0x00u, 0xA0u, 0xC9u, 0x69u, 0x72u, 0x3Bu}};
EFI_GUID gEfiFileInfoGuid = {0x09576E92u, 0x6D3Fu, 0x11d2u, {0x8Eu, 0x39u, 0x00u, 0xA0u, 0xC9u, 0x69u, 0x72u, 0x3Bu}};
EFI_GUID gEfiBlockIoProtocolGuid = {0x964E5B21u, 0x6459u, 0x11d2u, {0x8Eu, 0x39u, 0x00u, 0xA0u, 0xC9u, 0x69u, 0x72u, 0x3Bu}};
EFI_GUID gEfiDevicePathProtocolGuid = {0x09576E91u, 0x6D3Fu, 0x11d2u, {0x8Eu, 0x39u, 0x00u, 0xA0u, 0xC9u, 0x69u, 0x72u, 0x3Bu}};
EFI_GUID gEfiGraphicsOutputProtocolGuid = {0x9042A9DEu, 0x23DCu, 0x4A38u, {0x96u, 0xFBu, 0x7Au, 0xDEu, 0xD0u, 0x80u, 0x51u, 0x6Au}};

static EFI_SYSTEM_TABLE* g_st = NULL;
static EFI_BOOT_SERVICES* g_bs = NULL;
static EFI_HANDLE g_image_handle = NULL;
static uint8_t g_memory_map_buffer[131072];

#define DEVICE_PATH_TYPE_MESSAGING 0x03u
#define DEVICE_PATH_TYPE_MEDIA 0x04u
#define DEVICE_PATH_TYPE_END 0x7Fu
#define DEVICE_PATH_SUBTYPE_ATAPI 0x01u
#define DEVICE_PATH_SUBTYPE_HARDDRIVE 0x01u
#define DEVICE_PATH_SUBTYPE_END_ENTIRE 0xFFu

typedef struct __attribute__((packed)) AtapiDevicePath {
    EFI_DEVICE_PATH_PROTOCOL header;
    uint8_t primary_secondary;
    uint8_t slave_master;
    uint16_t lun;
} AtapiDevicePath;

typedef struct __attribute__((packed)) HardDriveDevicePath {
    EFI_DEVICE_PATH_PROTOCOL header;
    uint32_t partition_number;
    uint64_t partition_start;
    uint64_t partition_size;
    uint8_t signature[16];
    uint8_t mbr_type;
    uint8_t signature_type;
} HardDriveDevicePath;

static void* mem_set(void* dst, uint8_t value, UINTN size)
{
    UINTN i;
    uint8_t* out = (uint8_t*)dst;

    for (i = 0; i < size; ++i)
        out[i] = value;
    return dst;
}

static void* mem_copy(void* dst, const void* src, UINTN size)
{
    UINTN i;
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;

    for (i = 0; i < size; ++i)
        out[i] = in[i];
    return dst;
}

static UINTN align_up(UINTN value, UINTN align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static UINTN device_path_length(const EFI_DEVICE_PATH_PROTOCOL* path)
{
    return (UINTN)path->Length[0] | ((UINTN)path->Length[1] << 8u);
}

static bool device_path_is_end(const EFI_DEVICE_PATH_PROTOCOL* path)
{
    return path->Type == DEVICE_PATH_TYPE_END && path->SubType == DEVICE_PATH_SUBTYPE_END_ENTIRE;
}

static const EFI_DEVICE_PATH_PROTOCOL* next_device_path_node(const EFI_DEVICE_PATH_PROTOCOL* path)
{
    return (const EFI_DEVICE_PATH_PROTOCOL*)((const uint8_t*)path + device_path_length(path));
}

static void puts16(const CHAR16* text)
{
    if (g_st != NULL && g_st->ConOut != NULL)
        g_st->ConOut->OutputString(g_st->ConOut, text);
}

static EFI_STATUS open_protocol(EFI_HANDLE handle, EFI_GUID* guid, void** interface)
{
    return g_bs->HandleProtocol(handle, guid, interface);
}

static EFI_STATUS read_file(EFI_FILE_PROTOCOL* root, const CHAR16* path, void** data_out, UINTN* size_out)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL* file = NULL;
    EFI_FILE_INFO* info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 260u;
    void* data = NULL;
    UINTN file_size;

    status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        puts16(L"open file failed\r\n");
        return status;
    }

    status = g_bs->AllocatePool(EfiLoaderData, info_size, (void**)&info);
    if (EFI_ERROR(status)) {
        file->Close(file);
        return status;
    }

    status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (status == EFI_BUFFER_TOO_SMALL) {
        g_bs->FreePool(info);
        status = g_bs->AllocatePool(EfiLoaderData, info_size, (void**)&info);
        if (EFI_ERROR(status)) {
            file->Close(file);
            return status;
        }
        status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    }
    if (EFI_ERROR(status)) {
        puts16(L"getinfo data failed\r\n");
        g_bs->FreePool(info);
        file->Close(file);
        return status;
    }

    file_size = info->FileSize;
    status = g_bs->AllocatePool(EfiLoaderData, file_size, &data);
    if (EFI_ERROR(status)) {
        g_bs->FreePool(info);
        file->Close(file);
        return status;
    }

    status = file->Read(file, &file_size, data);
    file->Close(file);
    g_bs->FreePool(info);
    if (EFI_ERROR(status)) {
        puts16(L"read file failed\r\n");
        g_bs->FreePool(data);
        return status;
    }

    *data_out = data;
    *size_out = file_size;
    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel_elf(const void* image, UINTN image_size, uintptr_t* entry_out)
{
    const Elf64Header* header = (const Elf64Header*)image;
    const Elf64ProgramHeader* program_headers;
    uint16_t i;

    if (image_size < sizeof(*header))
        return 1;
    if (header->magic != ELF_MAGIC || header->ident_class != ELFCLASS64 || header->machine != EM_X86_64)
        return 1;
    if (header->phentsize != sizeof(Elf64ProgramHeader))
        return 1;
    if (header->phoff + (uint64_t)header->phnum * header->phentsize > image_size)
        return 1;

    program_headers = (const Elf64ProgramHeader*)((const uint8_t*)image + header->phoff);
    for (i = 0; i < header->phnum; ++i) {
        const Elf64ProgramHeader* segment = (const Elf64ProgramHeader*)((const uint8_t*)program_headers + (UINTN)i * header->phentsize);
        EFI_PHYSICAL_ADDRESS segment_base;
        UINTN pages;
        UINTN copy_size;

        if (segment->type != PT_LOAD)
            continue;
        if (segment->offset + segment->filesz > image_size || segment->filesz > segment->memsz)
            return 1;

        segment_base = (EFI_PHYSICAL_ADDRESS)(segment->paddr & ~0xFFFu);
        pages = (align_up((UINTN)(segment->memsz + (segment->paddr & 0xFFFu)), 4096u)) / 4096u;
        if (EFI_ERROR(g_bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EfiLoaderData, pages, &segment_base)))
            return EFI_OUT_OF_RESOURCES;

        mem_set((void*)(uintptr_t)segment->paddr, 0, (UINTN)segment->memsz);
        copy_size = (UINTN)segment->filesz;
        mem_copy((void*)(uintptr_t)segment->paddr, (const uint8_t*)image + segment->offset, copy_size);
    }

    *entry_out = (uintptr_t)header->entry;
    return EFI_SUCCESS;
}

static EFI_STATUS query_console(UINTN* cols_out, UINTN* rows_out)
{
    UINTN cols = 0;
    UINTN rows = 0;

    if (g_st->ConOut == NULL || g_st->ConOut->Mode == NULL)
        return 1;
    if (EFI_ERROR(g_st->ConOut->QueryMode(g_st->ConOut, (UINTN)g_st->ConOut->Mode->Mode, &cols, &rows)))
        return 1;

    *cols_out = cols;
    *rows_out = rows;
    return EFI_SUCCESS;
}

static EFI_STATUS gather_graphics_info(BootInfo* boot_info)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS status;

    if (boot_info == NULL)
        return 1;

    status = open_protocol(g_st->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (void**)&gop);
    if (EFI_ERROR(status) || gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL)
        return status;

    boot_info->framebuffer_base = (uintptr_t)gop->Mode->FrameBufferBase;
    boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
    boot_info->framebuffer_pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;

    if (gop->Mode->Info->PixelFormat == 0u)
        boot_info->framebuffer_format = BOOTINFO_FRAMEBUFFER_FORMAT_RGBX;
    else if (gop->Mode->Info->PixelFormat == 1u)
        boot_info->framebuffer_format = BOOTINFO_FRAMEBUFFER_FORMAT_BGRX;
    else
        return 1;

    boot_info->console_flags |= BOOTINFO_CONSOLE_FRAMEBUFFER;
    return EFI_SUCCESS;
}

static EFI_STATUS gather_block_info(EFI_HANDLE device, uint16_t* bytes_per_sector, uint32_t* sector_count)
{
    EFI_BLOCK_IO_PROTOCOL* block_io = NULL;
    EFI_STATUS status = open_protocol(device, &gEfiBlockIoProtocolGuid, (void**)&block_io);

    if (EFI_ERROR(status) || block_io == NULL || block_io->Media == NULL)
        return status;

    *bytes_per_sector = (uint16_t)block_io->Media->BlockSize;
    *sector_count = (uint32_t)(block_io->Media->LastBlock + 1u);
    return EFI_SUCCESS;
}

static EFI_STATUS gather_boot_device_info(EFI_HANDLE device, uint8_t* boot_drive_out, uint32_t* partition_lba_out)
{
    EFI_DEVICE_PATH_PROTOCOL* device_path = NULL;
    EFI_STATUS status = open_protocol(device, &gEfiDevicePathProtocolGuid, (void**)&device_path);
    const EFI_DEVICE_PATH_PROTOCOL* node;

    if (EFI_ERROR(status))
        return status;
    if (device_path == NULL)
        return 1;

    *boot_drive_out = 0x80u;
    *partition_lba_out = 0u;
    for (node = device_path; !device_path_is_end(node); node = next_device_path_node(node)) {
        UINTN node_length = device_path_length(node);

        if (node_length < sizeof(EFI_DEVICE_PATH_PROTOCOL))
            return 1;
        if (node->Type == DEVICE_PATH_TYPE_MESSAGING
            && node->SubType == DEVICE_PATH_SUBTYPE_ATAPI
            && node_length >= sizeof(AtapiDevicePath)) {
            const AtapiDevicePath* atapi = (const AtapiDevicePath*)node;

            if (atapi->primary_secondary <= 1u && atapi->slave_master <= 1u)
                *boot_drive_out = (uint8_t)(0x80u + atapi->primary_secondary * 2u + atapi->slave_master);
        } else if (node->Type == DEVICE_PATH_TYPE_MEDIA
            && node->SubType == DEVICE_PATH_SUBTYPE_HARDDRIVE
            && node_length >= sizeof(HardDriveDevicePath)) {
            const HardDriveDevicePath* drive_path = (const HardDriveDevicePath*)node;

            if (drive_path->partition_start > 0xFFFFFFFFu)
                return 1;
            *partition_lba_out = (uint32_t)drive_path->partition_start;
        }
    }

    return EFI_SUCCESS;
}

static EFI_STATUS exit_boot_services_with_map(EFI_HANDLE image_handle, BootInfo* boot_info)
{
    EFI_STATUS status;
    UINTN memory_map_size = sizeof(g_memory_map_buffer);
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;
    uint32_t attempts = 0;

    while (attempts++ < 8u) {
        memory_map_size = sizeof(g_memory_map_buffer);
        status = g_bs->GetMemoryMap(&memory_map_size, (EFI_MEMORY_DESCRIPTOR*)g_memory_map_buffer, &map_key, &descriptor_size, &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL)
            return status;
        if (EFI_ERROR(status))
            return status;

        boot_info->memory_map.base = (uintptr_t)g_memory_map_buffer;
        boot_info->memory_map.size = memory_map_size;
        boot_info->memory_map.descriptor_size = (uint32_t)descriptor_size;
        boot_info->memory_map.descriptor_version = descriptor_version;

        status = g_bs->ExitBootServices(image_handle, map_key);
        if (!EFI_ERROR(status))
            return status;
    }

    return status;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    EFI_HANDLE boot_device = NULL;
    void* kernel_image = NULL;
    UINTN kernel_size = 0;
    uintptr_t kernel_entry = 0;
    UINTN columns = 80;
    UINTN rows = 25;
    BootInfo* boot_info = NULL;
    KernelEntry kernel_start;

    g_st = system_table;
    g_bs = system_table->BootServices;
    g_image_handle = image_handle;

    puts16(L"FunnyOS UEFI boot\r\n");

    status = open_protocol(image_handle, &gEfiLoadedImageProtocolGuid, (void**)&loaded_image);
    if (EFI_ERROR(status))
        return status;
    puts16(L"loaded image\r\n");
    if (loaded_image == NULL || loaded_image->DeviceHandle == NULL)
        return 1;
    boot_device = loaded_image->DeviceHandle;
    status = open_protocol(boot_device, &gEfiSimpleFileSystemProtocolGuid, (void**)&filesystem);
    if (EFI_ERROR(status))
        return status;
    puts16(L"opened fs\r\n");
    status = filesystem->OpenVolume(filesystem, &root);
    if (EFI_ERROR(status))
        return status;
    puts16(L"opened volume\r\n");

    status = read_file(root, L"\\KERNEL.ELF", &kernel_image, &kernel_size);
    if (EFI_ERROR(status)) {
        puts16(L"Missing KERNEL.ELF\r\n");
        return status;
    }
    puts16(L"read kernel\r\n");

    status = load_kernel_elf(kernel_image, kernel_size, &kernel_entry);
    if (EFI_ERROR(status)) {
        if (status == EFI_OUT_OF_RESOURCES)
            puts16(L"Kernel load address unavailable\r\n");
        else
            puts16(L"Invalid kernel ELF\r\n");
        return 1;
    }
    puts16(L"loaded kernel\r\n");

    status = g_bs->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&boot_info);
    if (EFI_ERROR(status))
        return status;
    puts16(L"allocated bootinfo\r\n");

    mem_set(boot_info, 0, sizeof(*boot_info));
    boot_info->magic = BOOTINFO_MAGIC;
    boot_info->boot_drive_number = 0x80u;
    boot_info->console_flags = BOOTINFO_CONSOLE_TEXT;
    if (!EFI_ERROR(query_console(&columns, &rows))) {
        boot_info->screen_columns = (uint16_t)columns;
        boot_info->screen_rows = (uint16_t)rows;
    }
    if (!EFI_ERROR(gather_graphics_info(boot_info)))
        boot_info->console_flags &= (uint16_t)~BOOTINFO_CONSOLE_VGA_TEXT;
    status = gather_boot_device_info(boot_device, &boot_info->boot_drive_number, &boot_info->partition_lba_start);
    if (EFI_ERROR(status))
        return status;
    status = gather_block_info(boot_device, &boot_info->bytes_per_sector, &boot_info->partition_sector_count);
    if (EFI_ERROR(status))
        return status;
    puts16(L"gathered device info\r\n");
    status = exit_boot_services_with_map(image_handle, boot_info);
    if (EFI_ERROR(status))
        return status;

    kernel_start = (KernelEntry)kernel_entry;
    kernel_start(boot_info);
    return EFI_SUCCESS;
}
