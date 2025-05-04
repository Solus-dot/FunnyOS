#pragma once

// 0x00000000 - 0x000003FF - Interrupt vector table
// 0x00000400 - 0x000004FF - BIOS data area

#define MEMORY_MIN      0x00000500
#define MEMORY_MAX      0x00080000

// 0x00000500 - 0x00100500 - FAT Driver
#define MEMORY_FAT_ADDR  ((void far*)0x05000000)     // Segment:Offset (SSSSOOOO)
#define MEMORY_FAT_SIZE  0x00010000

// 0x00020000 - 0x00030000 - Stage2

// 0x00030000 - 0x00080000 - Free

// 0x00080000 - 0x0009FFFF - Extended BIOS data area
// 0x000A0000 - 0x000C7FFF - Video
// 0x000C8000 - 0x000FFFFF - BIOS
