[ORG 0x7C00]
[BITS 16]

%define BUFFER_ADDR         0x7E00
%define STAGE2_LOAD_SEGMENT 0x3000

%define BPB_SECTORS_PER_CLUSTER_OFF 13
%define BPB_RESERVED_SECTORS_OFF    14
%define BPB_FAT_COUNT_OFF           16
%define BPB_DIR_ENTRY_COUNT_OFF     17
%define BPB_SECTORS_PER_FAT_OFF     22
%define BPB_HIDDEN_SECTORS_OFF      28

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

    mov ax, [0x7C00 + BPB_DIR_ENTRY_COUNT_OFF]
    shl ax, 5
    add ax, 511
    shr ax, 9
    mov [root_left], ax

    xor ax, ax
    mov al, [0x7C00 + BPB_FAT_COUNT_OFF]
    mul word [0x7C00 + BPB_SECTORS_PER_FAT_OFF]
    add ax, [0x7C00 + BPB_RESERVED_SECTORS_OFF]
    movzx eax, ax
    mov [data_lba_rel], eax
    movzx edx, word [root_left]
    add [data_lba_rel], edx
    add eax, [0x7C00 + BPB_HIDDEN_SECTORS_OFF]
    mov [current_lba], eax

find_stage2:
    cmp word [root_left], 0
    je fail

    mov eax, [current_lba]
    xor bx, bx
    mov es, bx
    mov bx, BUFFER_ADDR
    mov cx, 1
    call read_lba
    jc fail

    mov di, BUFFER_ADDR
    mov cx, 16

.next_entry:
    cmp byte [di], 0
    je fail
    push cx
    push di
    push si
    mov si, stage2_name
    mov cx, 11
    repe cmpsb
    pop si
    pop di
    pop cx
    je .found
    add di, 32
    loop .next_entry

    inc dword [current_lba]
    dec word [root_left]
    jmp find_stage2

.found:
    mov ax, [di + 26]
    mov [current_cluster], ax
    mov ax, STAGE2_LOAD_SEGMENT
    mov es, ax
    xor bx, bx

load_stage2:
    mov ax, [current_cluster]
    cmp ax, 2
    jb fail
    cmp ax, 0xFFF8
    jae boot_stage2

    movzx eax, ax
    sub eax, 2
    xor ecx, ecx
    mov cl, [0x7C00 + BPB_SECTORS_PER_CLUSTER_OFF]
    imul eax, ecx
    add eax, [data_lba_rel]
    add eax, [0x7C00 + BPB_HIDDEN_SECTORS_OFF]
    xor cx, cx
    mov cl, [0x7C00 + BPB_SECTORS_PER_CLUSTER_OFF]
    call read_lba
    jc fail

    mov ax, es
    xor dx, dx
    mov dl, [0x7C00 + BPB_SECTORS_PER_CLUSTER_OFF]
    shl dx, 5
    add ax, dx
    mov es, ax

    xor eax, eax
    mov ax, [current_cluster]
    shl eax, 1
    shr eax, 9
    movzx ecx, word [0x7C00 + BPB_RESERVED_SECTORS_OFF]
    add eax, ecx
    add eax, [0x7C00 + BPB_HIDDEN_SECTORS_OFF]
    push es
    xor bx, bx
    mov es, bx
    mov bx, BUFFER_ADDR
    mov cx, 1
    call read_lba
    pop es
    jc fail

    mov bx, [current_cluster]
    shl bx, 1
    and bx, 0x01FF
    add bx, BUFFER_ADDR
    mov ax, [bx]
    mov [current_cluster], ax
    jmp load_stage2

boot_stage2:
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

msg_failed:      db 'Boot failed', 0
stage2_name:     db 'STAGE2  BIN'
boot_drive:      db 0
current_cluster: dw 0
root_left:       dw 0
current_lba:     dd 0
data_lba_rel:    dd 0

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
