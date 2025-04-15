[BITS 16]                   ; Bootloader running in 16 bit mode (Real mode)

section _TEXT class=CODE

;
; int 10h ah=0Eh
; args: character, page
;
global _x86_Video_WriteCharTeletype
_x86_Video_WriteCharTeletype:

    ; Make new call frame
    push bp             ; Save old call frame
    mov bp, sp          ; Initialize new call frame

    ; [bp + 0] -> Old call frame
    ; [bp + 2] -> Return address (small memory model = 2 bytes)
    ; [bp + 4] -> First argument (Character)
    ; [bp + 6] -> Second argument (Page)
    ; Note: Bytes are converted to words (you cannot push a single byte on the stack)
    mov ah, 0Eh
    mov al, [bp + 4]
    mov bh, [bp + 6]

    int 10h

    ; Restore bx
    pop bx
    ; Restore old call frame
    mov sp, bp
    pop bp
    ret
