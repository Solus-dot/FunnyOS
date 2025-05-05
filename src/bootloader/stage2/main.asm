[BITS 16]                   ; Bootloader running in 16 bit mode (Real mode)

section _ENTRY class=CODE   ; We will place everything next in the entry section

; Name mangling: in CDECL convention, C functions are prepended with a _
extern _cstart_         ; External cstart -> Entry point from C
global entry            ; Entry is visible outside this asm file

entry:
    cli
    ; Setup Stack
    mov ax, ds
    mov ss, ax
    mov sp, 0           ; Set pointers to 0 as boot.asm has already managed the work for it
    mov bp, sp
    sti

    ; Expect boot drive in dl, send it as argument to cstart function
    xor dh, dh
    push dx
    call _cstart_

    cli
    hlt                 ; If we ever return from cstart, we will hold the system for safety
