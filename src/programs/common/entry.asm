[BITS 32]

global _start
extern g_program_api
extern g_program_info
extern program_main

section .text
_start:
    mov [g_program_api], eax
    mov [g_program_info], ebx
    push ebx
    push eax
    call program_main
    add esp, 8

    mov ebx, [g_program_api]
    push eax
    call dword [ebx]

.halt:
    hlt
    jmp .halt
