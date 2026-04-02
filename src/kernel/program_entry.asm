[BITS 32]

global program_invoke
global program_exit_resume

section .bss
align 4
program_resume_esp:
    resd 1

section .text
program_invoke:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov [program_resume_esp], esp

    mov esi, [ebp + 8]
    mov eax, [ebp + 12]
    mov ebx, [ebp + 16]
    mov esp, [ebp + 20]
    call esi
    xor eax, eax
    jmp program_restore

program_exit_resume:
    mov eax, 1

program_restore:
    mov esp, [program_resume_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
