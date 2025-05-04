[BITS 16]                   ; Bootloader running in 16 bit mode (Real mode)

section _TEXT class=CODE

;
; U4D
;
; Operation:      Unsigned 4 byte divide
; Inputs:         DX;AX   Dividend
;                 CX;BX   Divisor
; Outputs:        DX;AX   Quotient
;                 CX;BX   Remainder
; Volatile:       none
;
global __U4D
__U4D:
    shl edx, 16         ; dx to the upper half of edx
    mov dx, ax          ; edx - Dividend
    mov eax, edx        ; eax - Dividend
    xor edx, edx        ; edx - 0

    shl ecx, 16         ; cx to the upper half of ecx
    mov cx, bx          ; ecx - Divisor
    
    div ecx             ; eax - Quotient, edx - Remainder
    mov ebx, edx        ;
    mov ecx, edx
    shr ecx, 16
    
    mov edx, eax
    shr edx, 16

    ret

; Name mangling: in CDECL convention, C functions are prepended with a _
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

;
; bool _cdecl x86_Disk_Reset(uint8_t drive);
;
global _x86_Disk_Reset
_x86_Disk_Reset:
    ; Make new call frame
    push bp             ; Save old call frame
    mov bp, sp          ; Initialize new call frame

    mov ah, 0
    mov dl, [bp + 4]    ; dl - Drive
    stc
    int 13h

    ; Set return value
    mov ax, 1
    sbb ax, 0           ; 1 = Success, 2 = Failure


    ; Restore old call frame
    mov sp, bp
    pop bp
    ret



; bool _cdecl x86_Disk_Read(uint8_t drive,
;                           uint16_t cylinder,
;                           uint16_t sector,
;                           uint16_t head,
;                           uint8_t count,
;                           uint8_t far * dataOut);
global _x86_Disk_Read
_x86_Disk_Read:
    ; Make new call frame
    push bp             ; Save old call frame
    mov bp, sp          ; Initialize new call frame

    ; Setup arguments
    mov dl, [bp + 4]    ; dl - Drive
    mov ch, [bp + 6]    ; ch - Cylinder (lower 8 bits)
    mov cl, [bp + 7]    ; cl - Cylinder to bits 6-7
    shl cl, 6

    mov dh, [bp + 10]    ; dh - Head

    mov al, [bp + 8]
    and al, 0x3F
    or cl, al           ; cl - Sector to bits 0-5

    mov al, [bp + 12]   ; al - Count

    mov bx, [bp + 16]   ; es:bx - Far pointer to data out
    mov es, bx
    mov bx, [bp + 14]

    ; Call interrupt 13h
    mov ah, 02h
    stc
    int 13h

    ; Set return value
    mov ax, 1
    sbb ax, 0           ; 1 = Success, 2 = Failure

    ; Restore registers
    pop es
    pop bx

    ; Restore old call frame
    mov sp, bp
    pop bp
    ret


; bool _cdecl x86_Disk_GetDriveParams(uint8_t drive,
;                                     uint8_t* driveTypeOut,
;                                     uint16_t* cylindersOut,
;                                     uint16_t* sectorsOut,
;                                     uint16_t* headsOut);
global _x86_Disk_GetDriveParams
_x86_Disk_GetDriveParams:
    ; Make new call frame
    push bp             ; Save old call frame
    mov bp, sp          ; Initialize new call frame

    ; Save registers
    push es
    push bx
    push si
    push di

    ; Call interrupt 13h
    mov dl, [bp + 4]    ; dl - Disk drive
    mov ah, 08h
    mov di, 0           ; es:di - 0000:0000
    mov es, di
    stc
    int 13h
    
    ; Return
    mov ax, 1
    sbb ax, 0

    ; Out params
    mov si, [bp + 6]    ; Drive type from bl
    mov [si], bl

    mov bl, ch          ; Cylinders - lowers bits in ch
    mov bh, cl          ; Cylinders - upper bits in cl (6-7)
    shr bh, 6
    mov si, [bp + 8]
    mov [si], bx

    xor ch, ch          ; Sectors - lower 5 bits in cl
    and cl, 0x3F
    mov si, [bp + 10]
    mov [si], cx

    mov cl, dh          ; Heads - dh
    mov si, [bp + 12]
    mov [si], cx

    ; Restore registers
    pop di
    pop si
    pop bx
    pop es

    ; Restore old call frame
    mov sp, bp
    pop bp
    ret