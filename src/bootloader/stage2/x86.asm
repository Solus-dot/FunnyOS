[BITS 16]                   ; Bootloader running in 16 bit mode (Real mode)

section _TEXT class=CODE

;
; void _cdecl x86_div64_32(uint64_t dividend, uint32_t divisor, uint64_t* quotientOut, uint32_t* remainderOut);
;
global _x86_div64_32
_x86_div64_32:
    ; Make new call frame
    push bp             ; Save old call frame
    mov bp, sp          ; Initialize new call frame

    push bx

    ; Divide upper 32 bits
    mov eax, [bp + 8]   ; eax -> Upper 32 bits of dividend
    mov ecx, [bp + 12]  ; ecx -> Divisor
    xor edx, edx
    div ecx             ; eax -> Quotient, edx -> Remainder

    ; Store upper 32 bits of quotient
    mov bx, [bp + 16]
    mov [bx], eax

    ; Divide lower 32 bits
    mov eax, [bp + 4]   ; eax <- Lower 32 bits of dividend
                        ; edx <- Old remainder
    div ecx

    ; Store results
    mov [bx], eax
    mov bx, [bp + 18]
    mov [bx], edx

    pop bx

    ; Restore old call frame
    mov sp, bp
    pop bp
    ret

;
; int 10h ah=0Eh
; args: character, page
; Name mangling: in CDECL convention, C functions are prepended with a _
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
