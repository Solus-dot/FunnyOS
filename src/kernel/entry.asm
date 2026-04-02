[BITS 32]

global _start
extern kmain

section .text
_start:
    cli
    mov esi, eax
    cld
    call setup_platform
    mov esp, stack_top
    push esi
    call kmain

.halt:
    hlt
    jmp .halt

setup_platform:
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    mov eax, interrupt_stub
    mov edx, eax
    shr edx, 16
    mov edi, idt_table
    mov ecx, 256

.fill_idt:
    mov word [edi], ax
    mov word [edi + 2], 0x08
    mov byte [edi + 4], 0
    mov byte [edi + 5], 0x8E
    mov word [edi + 6], dx
    add edi, 8
    loop .fill_idt

    lidt [idt_descriptor]
    ret

interrupt_stub:
    iretd

section .data
align 8
idt_descriptor:
    dw (256 * 8) - 1
    dd idt_table

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
align 8
idt_table:
    resb 256 * 8
