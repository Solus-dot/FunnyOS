[BITS 64]

global program_invoke
global program_exit_resume

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

    mov [rel program_resume_rsp], rsp

    mov r12, rdi
    mov rdi, rsi
    mov rsp, rdx
    call r12
    xor eax, eax
    jmp program_restore

program_exit_resume:
    mov eax, 1

program_restore:
    mov rsp, [rel program_resume_rsp]
    pop r12
    pop rbx
    pop rbp
    ret
