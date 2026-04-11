OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x00200000;

    .text ALIGN(16) :
    {
        __text_start = .;
        *(.text*)
        __text_end = .;
    }

    . = ALIGN(4096);
    .rodata ALIGN(4096) :
    {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    }

    . = ALIGN(4096);
    .data ALIGN(4096) :
    {
        __data_start = .;
        *(.data*)
        __data_end = .;
    }

    . = ALIGN(4096);
    .bss ALIGN(4096) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_image_end = .;
}
