[ORG 0x7C00]            ; This directive (not instruction) tells BIOS to load bootloader at 0x7C00
[BITS 16]               ; Bootloader running in 16 bit mode (Real mode)

%define ENDL 0x0D, 0x0A

;
; FAT 12 HEADERS
;

jmp short start
nop

bdb_oem:                    db 'MSWIN4.1'               ; 8 bytes
bdb_bytes_per_sector:       dw 512
bdb_sectors_per_cluster:    db 1
bdb_reserved_sectors:       dw 1
bdb_fat_count:              db 2
bdb_dir_entries_count:      dw 0xE0
bdb_total_sectors:          dw 2880                     ; 2880 * 512 = 1.44MB
bdb_media_descriptor:       db 0xF0                     ; 0xF0 = 3.5" floppy disk
bdb_sectors_per_fat:        dw 9
bdb_sectors_per_track:      dw 18
bdb_heads:                  dw 2
bdb_hidden_sectors:         dd 0
bdb_large_sector_count:     dd 0

; Extended Boot Record
ebr_drive_number:           db 0                        ; 0x00 floppy, 0x80 hdd
                            db 0                        ; Reserved byte
ebr_signature:              db 0x29
ebr_volume_id:              db 0x11, 0x69, 0x42, 0x13   ; Serial number, values don't matter
ebr_volume_label:           db 'FunnyOS    '            ; 11 bytes, padded with spaces
ebr_system_id:              db 'FAT12   '               ; 8 bytes, padded with spaces


start:
    jmp main

; Prints a string to the screen
; Params:
;   - ds:si points to string
puts:
    ; Save registers that are to be modified
    push si
    push ax

.loop:
    lodsb               ; Loads next character in al
    or al, al           ; verify if next character is null (al == 0)
    jz .done            ; If null character, finish the function

    mov ah, 0x0E        ; Call BIOS Interrupt
    mov bh, 0
    int 0x10
    jmp .loop           ; Loop back until al == 0 [string has been fully printed]

.done:
    ; Remove saved registers after function is done executing
    pop ax
    pop si
    ret


main:
    ; Setup data segments
    mov ax, 0           ; Cannot write to ds/es directly\
    mov ds, ax
    mov es, ax

    ; Setup stack
    mov ss, ax
    mov sp, 0x7C00      ; stack grows downwards from where we loaded our memory

    ; Read something from the floppy disk
    ; BIOS should set DL to drive number
    mov [ebr_drive_number], dl

    mov ax, 1           ; LBA=1, second sector from disk
    mov cl, 1           ; 1 sector to read
    mov bx, 0x7E00      ; Data should be after the bootloader
    call disk_read

    ; Print message1
    mov si, message1
    call puts

    cli                 ; Disables interrupts so that CPU does not get out of halt state
    hlt                 ; Stops CPU from executing

;
; ERROR HANDLERS
;

floppy_error:
    mov si, msg_read_failed
    call puts
    jmp wait_key_and_reboot

wait_key_and_reboot:
    mov ah, 0
    int 0x16            ; Wait for keypress
    jmp 0FFFFh:0        ; Jump to beginning to BIOS, should reboot

.halt:
    cli                 ; Disables interrupts so that CPU does not get out of halt state
    hlt                 

;
; DISK ROUTINES
;

; Converts an LBA (Logical Block Addressing) address to CHS (Cylinder, Head, Sector) Address
; Parameters:
;   - ax: LBA Address
; Returns:
;   - cx [bits 0-5]: sector number
;   - cx [bits 6-15]: cylinder
;   - dh: head
lba_to_chs:
    push ax
    push dx

    xor dx, dx                          ; dx = 0
    div word [bdb_sectors_per_track]    ; ax = LBA / SectorsPerTrack
                                        ; dx = LBA % SectorsPerTrack
    inc dx                              ; dx = (LBA % SectorsPerTrack) + 1 = sector
    mov cx, dx                          ; cx = sector

    xor dx, dx                          ; dx = 0
    div word [bdb_heads]                ; ax = (LBA / SectorsPerTrack) / Heads = cylinder
                                        ; dx = (LBA / SectorsPerTrack) % Heads = head
    mov dh, dl                          ; dh = head
    mov ch, al                          ; ch = cylinder (lower 8 bits)
    shl ah, 6                           ; shl left shifts bits (<< in C)
    or cl, ah                           ; Put upper 2 bits of cylinder in CL

    pop ax
    mov dl, al                          ; Restore DL
    pop ax
    ret

; Reads sectors from a disk
; Parameters:
;   - ax: LBA address
;   - cl: Number of sectors to read (upto 128)
;   - dl: Drive number
;   - es:bx: Memory address to store read data
disk_read:
    push ax                             ; Save registers to be modified
    push bx
    push cx
    push dx
    push di

    push cx                             ; Temporarily save CL (Number of sectors to read)
    call lba_to_chs                     ; Compute CHS
    pop ax                              ; AL = number of sectors to read

    mov ah, 0x02
    mov di, 3                           ; retry count

.retry:
    pusha                               ; Save all registers, we do not know what BIOS Modifies
    stc                                 ; Set carry flag, Some BIOS'es do not set it
    int 0x13                            ; Carry flag cleared = success
    jnc .done                           ; Jump if carry not set

    ; Read failed
    popa
    call disk_reset
    dec di
    test di, di
    jnz .retry

.fail:
    ; All attempts are exhausted
    jmp floppy_error

.done:
    popa

    pop di                             ; Save modified registers
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Resets disk controller
; Parameters:
;   - dl: Drive number
disk_reset:
    pusha
    mov ah, 0
    stc
    int 0x13
    jc floppy_error
    popa
    ret



message1:               db 'Hello Guys, FunnyOS Here!', ENDL, 0
msg_read_failed:        db 'Read from disk failed!', ENDL, 0

times 510-($-$$) db 0   ; Fill remaining space with zeros (510 bytes + the boot signature bytes 0x55 and 0xAA, ensuring 512 bytes in boot sector)
dw 0xAA55               ; boot signature