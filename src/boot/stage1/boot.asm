[ORG 0x7C00]
[BITS 16]

%define STAGE2_LOAD_SEGMENT 0x3000

    jmp short start
    nop

oem_label:            db 'FUNNYOS '
bytes_per_sector:     dw 512
sectors_per_cluster:  db 4
reserved_sectors:     dw 1
fat_count:            db 2
root_entry_count:     dw 512
total_sectors_16:     dw 0
media_descriptor:     db 0xF8
sectors_per_fat:      dw 126
sectors_per_track:    dw 63
head_count:           dw 16
hidden_sectors:       dd 2048
total_sectors_32:     dd 129024
drive_number:         db 0x80
reserved1:            db 0
boot_signature:       db 0x29
volume_id:            dd 0x46554E4F
volume_label:         db 'FunnyOS    '
filesystem_type:      db 'FAT16   '
stage2_lba:           dd 0
stage2_sector_count:  dw 0
stage2_reserved:      dw 0

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov cx, [stage2_sector_count]
    cmp cx, 0
    je fail

    mov ax, STAGE2_LOAD_SEGMENT
    mov es, ax
    xor bx, bx
    mov eax, [stage2_lba]
    cmp eax, 0
    je fail
    call read_lba
    jc fail

    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEGMENT:0

read_lba:
    push eax
    mov [disk_packet.count], cx
    mov [disk_packet.offset], bx
    mov dx, es
    mov [disk_packet.segment], dx
    pop eax
    mov [disk_packet.lba_low], eax
    mov dword [disk_packet.lba_high], 0
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    ret

fail:
    mov si, msg_failed
    call puts
.halt:
    cli
    hlt
    jmp .halt

puts:
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .next
.done:
    ret

msg_failed: db 'Boot failed', 0
boot_drive: db 0

disk_packet:
    db 0x10
    db 0
.count:
    dw 0
.offset:
    dw 0
.segment:
    dw 0
.lba_low:
    dd 0
.lba_high:
    dd 0

times 510 - ($ - $$) db 0
dw 0xAA55
