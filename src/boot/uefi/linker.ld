OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

SECTIONS
{
    . = 0;

    .text ALIGN(16) :
    {
        *(.text*)
    }

    .rodata ALIGN(16) :
    {
        *(.rodata*)
        *(.data.rel.ro*)
    }

    .data ALIGN(16) :
    {
        *(.data*)
    }

    .dynamic ALIGN(16) :
    {
        *(.dynamic)
    }

    .rela ALIGN(16) :
    {
        *(.rela*)
    }

    .dynsym ALIGN(16) :
    {
        *(.dynsym)
    }

    .dynstr ALIGN(16) :
    {
        *(.dynstr*)
    }

    .reloc ALIGN(16) :
    {
        *(.reloc)
    }

    .bss ALIGN(16) :
    {
        *(COMMON)
        *(.bss*)
    }
}
