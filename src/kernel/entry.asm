[BITS 64]

global _start
extern kmain
extern interrupt_dispatch
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
    xor r9d, r9d
    mov r9w, cs

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

    lea rsi, [rel interrupt_stub_table]
    lea rdi, [rel idt_table]
    mov ecx, 256

.fill_idt:
    mov rax, [rsi]
    mov rdx, rax
    shr rdx, 16
    mov r8, rax
    shr r8, 32
    mov word [rdi], ax
    mov word [rdi + 2], r9w
    mov byte [rdi + 4], 0
    mov byte [rdi + 5], 0x8E
    mov word [rdi + 6], dx
    mov dword [rdi + 8], r8d
    mov dword [rdi + 12], 0
    add rsi, 8
    add rdi, 16
    loop .fill_idt

    lidt [rel idt_descriptor]
    ret

exception_common:
    cld
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax
    lea rax, [rsp + 160]
    push rax
    mov rbx, rsp
    mov rdi, rsp
    and rsp, -16
    call interrupt_dispatch
    mov rsp, rbx
    add rsp, 8
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

%macro ISR_NOERR 1
global isr_%1
isr_%1:
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro ISR_ERR 1
global isr_%1
isr_%1:
    push qword %1
    jmp exception_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

%assign i 32
%rep 224
ISR_NOERR i
%assign i i + 1
%endrep

section .rodata
align 16
idt_descriptor:
    dw (256 * 16) - 1
    dq idt_table

align 8
interrupt_stub_table:
%assign i 0
%rep 256
    dq isr_%+i
%assign i i + 1
%endrep
section .bss
align 16
idt_table:
    resb 256 * 16
align 16
stack_bottom:
    resb 16384
stack_top:
