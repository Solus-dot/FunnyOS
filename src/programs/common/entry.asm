[BITS 64]

global _start
extern g_program_api
extern g_program_info
extern program_main

section .text
_start:
    mov [rel g_program_api], rdi
    mov [rel g_program_info], rsi
    sub rsp, 8
    call program_main
    add rsp, 8

    mov rbx, [rel g_program_api]
    mov edi, eax
    call qword [rbx]

.halt:
    hlt
    jmp .halt
