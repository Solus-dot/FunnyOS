[ORG 0x00]
[BITS 16]               ; Bootloader running in 16 bit mode (Real mode)

%define ENDL 0x0D, 0x0A

start:
    ; Print intro message
    mov si, message1
    call puts

.halt:
    cli
    hlt                 ; Stops CPU from executing



; Prints a string to the screen
; Params:
;   - ds:si points to string
puts:
    ; save registers that are to be modified
    push si
    push ax

.loop:
    lodsb               ; Loads next character in al
    or al, al           ; verify if next character is null (al == 0)
    jz .done            ; if null character, finish the function

    mov ah, 0x0E        ; Call BIOS Interrupt
    mov bh, 0
    int 0x10
    jmp .loop           ; loop back until al == 0 [string has been fully printed]

.done:
    ; remove saved registers after function is done executing
    pop ax
    pop si
    ret

message1: db 'Hello Guys, FunnyOS Here!', ENDL, 0