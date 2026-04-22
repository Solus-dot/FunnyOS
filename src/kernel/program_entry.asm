[BITS 64]

global program_invoke
global program_exit_resume
global program_return_to_kernel
extern platform_set_tss_rsp0

USER_DATA_SELECTOR equ 0x1B
USER_CODE_SELECTOR equ 0x23

section .bss
align 8
program_resume_rsp:
    resq 1

section .text
program_invoke:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13

    mov [rel program_resume_rsp], rsp

    mov r12, rdi
    mov r13, rsi
    mov rdi, rsp
    call platform_set_tss_rsp0

    mov ax, USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword USER_DATA_SELECTOR
    push rdx
    pushfq
    or qword [rsp], 0x200
    push qword USER_CODE_SELECTOR
    push r12
    mov rdi, r13
    iretq

program_exit_resume:
    mov eax, 1

program_return_to_kernel:
    mov eax, 1
    jmp program_restore

program_restore:
    mov rsp, [rel program_resume_rsp]
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
