[BITS 64]

global _start
extern kmain
extern __bss_start
extern __bss_end

section .text
_start:
    cli
    mov rbx, rdi
    cld
    call clear_bss
    mov rsp, stack_top
    call setup_platform
    mov rdi, rbx
    call kmain

.halt:
    hlt
    jmp .halt

clear_bss:
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb
    ret

setup_platform:
    mov rax, cr0
    and rax, ~0x4
    or rax, 0x2
    mov cr0, rax

    mov rax, cr4
    or rax, 0x600
    mov cr4, rax
    fninit

    mov al, 0xFF
    out 0x21, al
    out 0xA1, al

    mov rax, interrupt_stub
    mov rdx, rax
    shr rdx, 16
    mov r8, rax
    shr r8, 32
    lea rdi, [rel idt_table]
    mov ecx, 256

.fill_idt:
    mov word [rdi], ax
    mov word [rdi + 2], CODE_SEL
    mov byte [rdi + 4], 0
    mov byte [rdi + 5], 0x8E
    mov word [rdi + 6], dx
    mov dword [rdi + 8], r8d
    mov dword [rdi + 12], 0
    add rdi, 16
    loop .fill_idt

    lidt [rel idt_descriptor]
    ret

interrupt_stub:
    iretq

section .data
align 16
idt_descriptor:
    dw (256 * 16) - 1
    dq idt_table

CODE_SEL equ 0x08

section .bss
align 16
idt_table:
    resb 256 * 16
align 16
stack_bottom:
    resb 16384
stack_top:
