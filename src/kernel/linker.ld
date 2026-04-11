OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x00200000;

    .text ALIGN(16) :
    {
        *(.text*)
    }

    .rodata ALIGN(16) :
    {
        *(.rodata*)
    }

    .data ALIGN(16) :
    {
        *(.data*)
    }

    .bss ALIGN(16) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_image_end = .;
}
