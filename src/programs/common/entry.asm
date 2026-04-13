[BITS 64]

global _start
extern g_program_info
extern program_main
extern program_exit

section .text
_start:
    mov [rel g_program_info], rdi
    mov rdi, [rel g_program_info]
    sub rsp, 8
    call program_main
    add rsp, 8

    mov edi, eax
    call program_exit

.halt:
    hlt
    jmp .halt
