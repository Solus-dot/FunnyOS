#ifndef FUNNYOS_UEFI_EFI_H
#define FUNNYOS_UEFI_EFI_H

#include "../../common/types.h"

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uintptr_t EFI_PHYSICAL_ADDRESS;
typedef uintptr_t EFI_VIRTUAL_ADDRESS;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint64_t EFI_LBA;

#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL 5
#define EFI_OUT_OF_RESOURCES 9
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x00000002u
#define EFI_FILE_MODE_READ 0x0000000000000001ull
#define EFI_FILE_DIRECTORY 0x0000000000000010ull
#define EFI_FILE_READ_ONLY 0x0000000000000001ull

#define EFI_ALLOCATE_ADDRESS 2u
#define EFI_BY_PROTOCOL 2u
#define EfiLoaderData 4u

#define EFI_ERROR(status) (((status) >> 63) != 0)

typedef struct EFI_GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

typedef struct __attribute__((packed)) EFI_DEVICE_PATH_PROTOCOL {
    uint8_t Type;
    uint8_t SubType;
    uint8_t Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

typedef struct EFI_TABLE_HEADER {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_INPUT_KEY {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct SIMPLE_TEXT_OUTPUT_MODE {
    int32_t MaxMode;
    int32_t Mode;
    int32_t Attribute;
    int32_t CursorColumn;
    int32_t CursorRow;
    bool CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS(EFIAPI* Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, bool);
    EFI_STATUS(EFIAPI* OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, const CHAR16*);
    EFI_STATUS(EFIAPI* TestString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, const CHAR16*);
    EFI_STATUS(EFIAPI* QueryMode)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, UINTN, UINTN*, UINTN*);
    EFI_STATUS(EFIAPI* SetMode)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, UINTN);
    EFI_STATUS(EFIAPI* SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, UINTN);
    EFI_STATUS(EFIAPI* ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
    EFI_STATUS(EFIAPI* SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, UINTN, UINTN);
    EFI_STATUS(EFIAPI* EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, bool);
    SIMPLE_TEXT_OUTPUT_MODE* Mode;
};

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS(EFIAPI* Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, bool);
    EFI_STATUS(EFIAPI* ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, EFI_INPUT_KEY*);
    void* WaitForKey;
};

typedef struct EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    uint32_t Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    EFI_RUNTIME_SERVICES* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL;
    void* RestoreTPL;
    EFI_STATUS(EFIAPI* AllocatePages)(uint32_t, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS*);
    EFI_STATUS(EFIAPI* FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS(EFIAPI* GetMemoryMap)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, uint32_t*);
    EFI_STATUS(EFIAPI* AllocatePool)(uint32_t, UINTN, void**);
    EFI_STATUS(EFIAPI* FreePool)(void*);
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS(EFIAPI* HandleProtocol)(EFI_HANDLE, EFI_GUID*, void**);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS(EFIAPI* ExitBootServices)(EFI_HANDLE, UINTN);
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    EFI_STATUS(EFIAPI* OpenProtocol)(EFI_HANDLE, EFI_GUID*, void**, EFI_HANDLE, EFI_HANDLE, uint32_t);
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    EFI_STATUS(EFIAPI* LocateHandleBuffer)(uint32_t, EFI_GUID*, void*, UINTN*, EFI_HANDLE**);
    void* LocateProtocol;
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    EFI_STATUS(EFIAPI* CalculateCrc32)(const void*, UINTN, uint32_t*);
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType;
    uint32_t ImageDataType;
    EFI_STATUS(EFIAPI* Unload)(EFI_HANDLE);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS(EFIAPI* Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, const CHAR16*, uint64_t, uint64_t);
    EFI_STATUS(EFIAPI* Close)(EFI_FILE_PROTOCOL*);
    EFI_STATUS(EFIAPI* Delete)(EFI_FILE_PROTOCOL*);
    EFI_STATUS(EFIAPI* Read)(EFI_FILE_PROTOCOL*, UINTN*, void*);
    EFI_STATUS(EFIAPI* Write)(EFI_FILE_PROTOCOL*, UINTN*, const void*);
    EFI_STATUS(EFIAPI* GetPosition)(EFI_FILE_PROTOCOL*, uint64_t*);
    EFI_STATUS(EFIAPI* SetPosition)(EFI_FILE_PROTOCOL*, uint64_t);
    EFI_STATUS(EFIAPI* GetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, UINTN*, void*);
    EFI_STATUS(EFIAPI* SetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, UINTN, const void*);
    EFI_STATUS(EFIAPI* Flush)(EFI_FILE_PROTOCOL*);
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS(EFIAPI* OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*, EFI_FILE_PROTOCOL**);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct EFI_FILE_INFO {
    UINTN Size;
    UINTN FileSize;
    UINTN PhysicalSize;
    uint64_t CreateTime[2];
    uint64_t LastAccessTime[2];
    uint64_t ModificationTime[2];
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct EFI_BLOCK_IO_MEDIA {
    uint32_t MediaId;
    bool RemovableMedia;
    bool MediaPresent;
    bool LogicalPartition;
    bool ReadOnly;
    bool WriteCaching;
    uint32_t BlockSize;
    uint32_t IoAlign;
    EFI_LBA LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL {
    uint64_t Revision;
    EFI_BLOCK_IO_MEDIA* Media;
    EFI_STATUS(EFIAPI* Reset)(struct EFI_BLOCK_IO_PROTOCOL*, bool);
    EFI_STATUS(EFIAPI* ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL*, uint32_t, EFI_LBA, UINTN, void*);
    EFI_STATUS(EFIAPI* WriteBlocks)(struct EFI_BLOCK_IO_PROTOCOL*, uint32_t, EFI_LBA, UINTN, const void*);
    EFI_STATUS(EFIAPI* FlushBlocks)(struct EFI_BLOCK_IO_PROTOCOL*);
} EFI_BLOCK_IO_PROTOCOL;

extern EFI_GUID gEfiLoadedImageProtocolGuid;
extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiFileInfoGuid;
extern EFI_GUID gEfiBlockIoProtocolGuid;
extern EFI_GUID gEfiDevicePathProtocolGuid;

#endif
