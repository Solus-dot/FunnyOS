#include "fat.h"
#include "stdio.h"
#include "memdefs.h"
#include "utility.h"
#include "string.h"
#include "memory.h"

#define SECTOR_SIZE             512
#define MAX_PATH_SIZE           256
#define MAX_FILE_HANDLES        10
#define ROOT_DIRECTORY_HANDLE   -1

#pragma pack(push, 1)
typedef struct 
{
    // FAT Headers
    uint8_t BootJumpInstruction[3];
    uint8_t OemIdentifier[8];
    uint16_t BytesPerSector;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FatCount;
    uint16_t DirEntryCount;
    uint16_t TotalSectors;
    uint8_t MediaDescriptorType;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t LargeSectorCount;

    // Extended Boot Record
    uint8_t DriveNumber;
    uint8_t Reserved;
    uint8_t Signature;
    uint32_t VolumeID;          // Serial number, values don't matter
    uint8_t VolumeLabel[11];    // 11 bytes, padded with spaces
    uint8_t SystemID[8];        // 8 bytes, padded with spaces

    // We Do not care about the code    

} FAT_BootSector;

#pragma pack(pop)

typedef struct
{
    FAT_File Public;
    bool Opened;
    uint32_t FirstCluster;
    uint32_t CurrentCluster;
    uint32_t CurrentSectorInCluster;
    uint8_t Buffer[SECTOR_SIZE];        // This buffer increases read speed as we do not have to repeat reads to get the bytes from the same sector.

} FAT_FileData;

typedef struct
{
    union
    {
        FAT_BootSector BootSector;
        uint8_t BootSectorBytes[SECTOR_SIZE];
    } BS;

    FAT_FileData RootDirectory;

    FAT_FileData OpenedFiles[MAX_FILE_HANDLES];
      
} FAT_Data;

static FAT_Data far* g_Data; 
static uint8_t far* g_Fat = NULL;
static uint32_t g_DataSectionLba;

bool FAT_ReadBootSector(DISK* disk)
{
    return DISK_ReadSectors(disk, 0, 1, &g_Data->BS.BootSectorBytes);
}

bool FAT_ReadFat(DISK* disk)
{
    return DISK_ReadSectors(disk, g_Data->BS.BootSector.ReservedSectors, g_Data->BS.BootSector.SectorsPerFat, g_Fat);
}

bool FAT_Initialize(DISK* disk)
{
    g_Data = (FAT_Data far*)MEMORY_FAT_ADDR;

    // Read Boot Sector
    if(!FAT_ReadBootSector(disk))
    {
        printf("FAT: read boot sector failed!\r\n");
        return false;
    }

    // Read FAT
    g_Fat = (uint8_t far*)g_Data + sizeof(FAT_Data);
    uint32_t fatSize = g_Data->BS.BootSector.BytesPerSector * g_Data->BS.BootSector.SectorsPerFat;
    if (sizeof(FAT_Data) + fatSize >= MEMORY_FAT_SIZE)
    {
        printf("FAT: not enough memory to read FAT! Required %lu, only have %u\r\n", sizeof(FAT_Data) + fatSize, MEMORY_FAT_SIZE);
        return false;
    }

     if (!FAT_ReadFat(disk))
    {
        printf("FAT: read FAT failed!\r\n");
        return false;
    }

    // Open Root Directory File
    uint32_t rootDirLba = g_Data->BS.BootSector.ReservedSectors + g_Data->BS.BootSector.SectorsPerFat * g_Data->BS.BootSector.FatCount;
    uint32_t rootDirSize = sizeof(FAT_DirectoryEntry) * g_Data->BS.BootSector.DirEntryCount;

    g_Data->RootDirectory.Public.Handle = ROOT_DIRECTORY_HANDLE;
    g_Data->RootDirectory.Public.IsDirectory = true;
    g_Data->RootDirectory.Public.Position = 0;
    g_Data->RootDirectory.Public.Size = sizeof(FAT_DirectoryEntry) * g_Data->BS.BootSector.DirEntryCount;;
    g_Data->RootDirectory.Opened = true;
    g_Data->RootDirectory.FirstCluster = 0;
    g_Data->RootDirectory.CurrentCluster = 0;
    g_Data->RootDirectory.CurrentSectorInCluster = 0;

    if (!DISK_ReadSectors(disk, rootDirLba, 1, g_Data->RootDirectory.Buffer))
    {
        printf("FAT: read root directory failed!\r\n");
        return false;
    }

    // Calculate Data Section
    uint32_t rootDirSectors = (rootDirSize + g_Data->BS.BootSector.BytesPerSector - 1) / g_Data->BS.BootSector.BytesPerSector;
    g_DataSectionLba = rootDirLba + rootDirSectors;

    // Reset opened files
    for(int i = 0; i < MAX_FILE_HANDLES; i++)
    {
        g_Data->OpenedFiles[i].Opened = false;
    }
}

uint32_t FAT_ClusterToLba(uint32_t cluster)
{
    return g_DataSectionLba + (cluster - 2) * g_Data->BS.BootSector.SectorsPerCluster;
}

FAT_File far* FAT_OpenEntry(DISK* disk, FAT_DirectoryEntry* entry)
{
    // Find Empty Handle
    int handle = -1;
    for (int i = 0; i < MAX_FILE_HANDLES && handle < 0; i++)
    {
        if (!g_Data->OpenedFiles[i].Opened)
            handle = i;
    }

    // Out of Handles
    if (handle < 0)
    {
        printf("FAT: Out of file handles!\r\n");
        return false;
    }

    // Setup Variables
    FAT_FileData far* fd = &g_Data->OpenedFiles[handle];
    fd->Public.Handle = handle;
    fd->Public.IsDirectory = (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
    fd->Public.Position = 0;
    fd->Public.Size = entry->Size;
    fd->FirstCluster = entry->FirstClusterLow + ((uint32_t)entry->FirstClusterHigh << 16);
    fd->CurrentCluster = fd->FirstCluster;
    fd->CurrentSectorInCluster = 0;

    if (!DISK_ReadSectors(disk, rootDirLba, 1, g_Data->RootDirectory.Buffer))
    {
        printf("FAT: read root directory failed\r\n");
        return false;
    }

    fd->Opened = true;
    return &fd->Public;
}

uint32_t FAT_Read(DISK* disk, FAT_File far* file, uint32_t byteCount, void* dataOut)
{
    // Get File Data
    
}

bool FAT_DiskEntry(DISK* disk, FAT_File far* file, FAT_DirectoryEntry* dirEntry)
{

}

void FAT_Close(FAT_File far* file)
{

}

bool FAT_FindFile(FAT_File far* file, const char* name, FAT_DirectoryEntry* entryOut)
{
    // for (uint32_t i = 0; i < g_BootSector.DirEntryCount; i++)
    // {
    //     if (memcmp(name, g_RootDirectory[i].Name, 11) == 0)
    //         return &g_RootDirectory[i];
    // }

    return NULL;
}


FAT_File far* FAT_Open(DISK* disk, const char* path)
{
    char name[MAX_PATH_SIZE];

    // Ignore Leading Slash
    if (path[0] == '/')
        path++;

    FAT_File far* current = &g_Data->RootDirectory.Public;

    while (*path)
    {
        // Extract next file name from path
        bool isLast = false;
        const char* delim = strchr(path, '/');
        if (delim != NULL)
        {
            memcpy(name, path, delim - path);
            name[delim - path + 1] = '\0';
            path = delim + 1;
        }
        else
        {
            unsigned len = strlen(path);
            memcpy(name, path, len);
            name[len + 1] = '\0';
            path += len;
            isLast = true;
        }

        // Find directory entry in current directory
        FAT_DirectoryEntry entry;
        if (FAT_FindFile(current, name, &entry))
        {
            FAT_Close(current);

            // Check If Directory
            if (!isLast && entry.Attributes & FAT_ATTRIBUTE_DIRECTORY == 0)
            {
                printf("FAT: %s not a directory\r\n", name);
                return NULL;
            }

            // Open New Directory Entry
            current = FAT_OpenEntry(disk, &entry);
        }
        else
        {
            FAT_Close(current);
            printf("FAT: %s not found\r\n", name);
            return NULL;
        }   
    }

    return current;
}
