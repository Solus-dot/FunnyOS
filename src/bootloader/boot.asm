[ORG 0x7C00]            ; This directive (not instruction) tells BIOS to load bootloader at 0x7C00
[BITS 16]               ; Bootloader running in 16 bit mode (Real mode)

%define ENDL 0x0D, 0x0A


; FAT 12 HEADERS
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
                            db 0                        ; reserved byte
ebr_signature:              db 0x29
ebr_volume_id:              db 11h, 69h, 42h, 13h       ; serial number, values don't matter
ebr_volume_label:           db 'FunnyOS    '            ; 11 bytes, padded with spaces
ebr_system_id:              db 'FAT12   '               ; 8 bytes, padded with spaces


start:
    jmp main

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


main:
    ; setup data segments
    mov ax, 0           ; Cannot write to ds/es directly\
    mov ds, ax
    mov es, ax

    ; setup stack
    mov ss, ax
    mov sp, 0x7C00      ; stack grows downwards from where we loaded our memory

    mov si, message1
    call puts

    hlt                 ; Stops CPU from executing

.halt:
    jmp .halt           ; Infinite Loop

message1: db 'Hello Guys, FunnyOS Here!', ENDL, 0

times 510-($-$$) db 0   ; Fill remaining space with zeros (510 bytes + the boot signature bytes 0x55 and 0xAA, ensuring 512 bytes in boot sector)
dw 0xAA55               ; boot signature