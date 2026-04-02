[ORG 0]
[BITS 16]

%define ENDL 0x0D, 0x0A

%define HANDOFF_ADDR        0x0600
%define BOOTINFO_ADDR       0x0900
%define BOOT_SECTOR_BUFFER  0x08000
%define FAT_BUFFER          0x10000
%define ROOT_DIR_BUFFER     0x20000
%define SUBDIR_BUFFER       0x24000
%define STAGE2_LOAD_ADDR    0x00030000
%define KERNEL_TEMP_ADDR    0x00050000
%define DEMO_FILE_BUFFER    0x00070000
%define KERNEL_LOAD_ADDR    0x00100000
%define RM_STACK_SEGMENT    0x7000
%define PM_STACK_TOP        0x00090000

%define BOOTINFO_MAGIC                   0x534F4E46
%define BOOTINFO_MAGIC_OFF               0
%define BOOTINFO_BOOT_DRIVE_OFF          4
%define BOOTINFO_PARTITION_INDEX_OFF     5
%define BOOTINFO_BYTES_PER_SECTOR_OFF    6
%define BOOTINFO_PARTITION_LBA_OFF       8
%define BOOTINFO_PARTITION_COUNT_OFF     12
%define BOOTINFO_SCREEN_COLS_OFF         16
%define BOOTINFO_SCREEN_ROWS_OFF         18

%define HANDOFF_BOOT_DRIVE_OFF           0
%define HANDOFF_PARTITION_INDEX_OFF      1
%define HANDOFF_PARTITION_LBA_OFF        4
%define HANDOFF_PARTITION_COUNT_OFF      8

%define DIR_ATTR_DIRECTORY               0x10
%define DIR_ATTR_VOLUME_ID               0x08
%define DIR_ATTR_LFN                     0x0F

%define BPB_BYTES_PER_SECTOR_OFF         11
%define BPB_SECTORS_PER_CLUSTER_OFF      13
%define BPB_RESERVED_SECTORS_OFF         14
%define BPB_FAT_COUNT_OFF                16
%define BPB_DIR_ENTRY_COUNT_OFF          17
%define BPB_TOTAL_SECTORS16_OFF          19
%define BPB_SECTORS_PER_FAT_OFF          22
%define BPB_HIDDEN_SECTORS_OFF           28
%define BPB_TOTAL_SECTORS32_OFF          32

%define DIR_NAME_OFF                     0
%define DIR_ATTRIBUTES_OFF               11
%define DIR_FIRST_CLUSTER_OFF            26
%define DIR_FILE_SIZE_OFF                28

start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ax, RM_STACK_SEGMENT
    mov ss, ax
    mov sp, 0xFFF0
    sti

    mov [boot_drive], dl
    xor ax, ax
    mov es, ax
    mov al, [es:HANDOFF_ADDR + HANDOFF_PARTITION_INDEX_OFF]
    mov [boot_partition_index], al
    mov eax, [es:HANDOFF_ADDR + HANDOFF_PARTITION_LBA_OFF]
    mov [partition_lba_start], eax
    mov eax, [es:HANDOFF_ADDR + HANDOFF_PARTITION_COUNT_OFF]
    mov [partition_sector_count], eax

    call serial_init
    mov si, msg_stage2
    call puts16

    call read_boot_sector
    jc boot_error
    mov si, msg_boot_ok
    call puts16

    call parse_boot_sector
    mov si, msg_parse_ok
    call puts16

    mov si, msg_root
    call puts16
    call read_root_directory
    jc root_error

    call read_fat_table
    jc fat_error

    mov si, kernel_file_name
    mov di, found_entry
    call find_root_entry
    jc kernel_not_found

    mov si, found_entry
    mov edi, KERNEL_TEMP_ADDR
    call load_file_from_entry
    jc kernel_read_error
    mov [kernel_size], eax

    mov si, msg_pm
    call puts16

    cli
    call enable_a20
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEL:(STAGE2_LOAD_ADDR + protected_mode_entry)

boot_error:
    mov si, msg_boot_error
    jmp fatal16

root_error:
    mov si, msg_root_error
    jmp fatal16

fat_error:
    mov si, msg_fat_error
    jmp fatal16

kernel_not_found:
    mov si, msg_kernel_missing
    jmp fatal16

kernel_read_error:
    mov si, msg_kernel_read
    jmp fatal16

fatal16:
    call puts16
.halt:
    cli
    hlt
    jmp .halt

read_boot_sector:
    mov eax, [partition_lba_start]
    mov edi, BOOT_SECTOR_BUFFER
    mov cx, 1
    call read_sector_count_to_phys
    ret

parse_boot_sector:
    mov bx, BOOT_SECTOR_BUFFER >> 4
    mov es, bx

    mov ax, [es:BPB_BYTES_PER_SECTOR_OFF]
    mov [bytes_per_sector], ax

    mov al, [es:BPB_SECTORS_PER_CLUSTER_OFF]
    mov [sectors_per_cluster], al

    mov ax, [es:BPB_RESERVED_SECTORS_OFF]
    mov [reserved_sectors], ax

    mov al, [es:BPB_FAT_COUNT_OFF]
    mov [fat_count], al

    mov ax, [es:BPB_DIR_ENTRY_COUNT_OFF]
    mov [dir_entry_count], ax

    mov ax, [es:BPB_SECTORS_PER_FAT_OFF]
    mov [sectors_per_fat], ax

    mov eax, [es:BPB_HIDDEN_SECTORS_OFF]
    mov [hidden_sectors], eax

    mov ax, [es:BPB_TOTAL_SECTORS16_OFF]
    test ax, ax
    jz .use_total32
    movzx eax, ax
    jmp .store_total

.use_total32:
    mov eax, [es:BPB_TOTAL_SECTORS32_OFF]

.store_total:
    mov [total_sectors], eax

    xor eax, eax
    mov ax, [dir_entry_count]
    shl eax, 5
    xor edx, edx
    movzx ecx, word [bytes_per_sector]
    div ecx
    test edx, edx
    jz .root_ready
    inc eax

.root_ready:
    mov [root_dir_sectors], eax

    movzx eax, word [sectors_per_fat]
    movzx ecx, byte [fat_count]
    imul eax, ecx
    movzx edx, word [reserved_sectors]
    add eax, edx
    mov [root_dir_lba_rel], eax

    mov edx, [root_dir_sectors]
    add eax, edx
    mov [data_lba_rel], eax

    movzx eax, byte [sectors_per_cluster]
    movzx ecx, word [bytes_per_sector]
    imul eax, ecx
    mov [cluster_size_bytes], eax
    ret

read_root_directory:
    mov eax, [partition_lba_start]
    add eax, [root_dir_lba_rel]
    mov edi, ROOT_DIR_BUFFER
    mov cx, [root_dir_sectors]
    call read_sector_count_to_phys
    ret

read_fat_table:
    mov eax, [partition_lba_start]
    xor edx, edx
    mov dx, [reserved_sectors]
    add eax, edx
    mov edi, FAT_BUFFER
    mov cx, [sectors_per_fat]
    call read_sector_count_to_phys
    ret

find_root_entry:
    mov bx, [dir_entry_count]
    mov dx, ROOT_DIR_BUFFER >> 4
    jmp find_entry_in_segment

find_subdir_entry:
    mov bx, [subdir_entry_count]
    mov dx, SUBDIR_BUFFER >> 4

find_entry_in_segment:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov bp, di
    mov ax, dx
    mov es, ax
    xor dx, dx
    xor cx, cx

.next_entry:
    cmp cx, bx
    jae .not_found

    mov di, dx
    mov al, [es:di]
    cmp al, 0
    je .not_found
    cmp al, 0xE5
    je .advance

    mov al, [es:di + DIR_ATTRIBUTES_OFF]
    cmp al, DIR_ATTR_LFN
    je .advance

    push cx
    push dx
    push si
    mov cx, 11
    mov di, dx

.compare_loop:
    mov al, [si]
    cmp al, [es:di]
    jne .compare_fail
    inc si
    inc di
    loop .compare_loop

    pop si
    pop dx
    pop cx
    jmp .found

.compare_fail:
    pop si
    pop dx
    pop cx

.advance:
    add dx, 32
    inc cx
    jmp .next_entry

.found:
    mov si, dx
    mov di, bp
    mov cx, 16
.copy_entry:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .copy_entry

    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

.not_found:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

load_directory_from_entry:
    mov bx, [si + DIR_FIRST_CLUSTER_OFF]
    mov edi, SUBDIR_BUFFER
    call load_cluster_chain_to_buffer
    jc .fail
    shr eax, 5
    mov [subdir_entry_count], ax
    clc
    ret

.fail:
    stc
    ret

load_file_from_entry:
    mov eax, [si + DIR_FILE_SIZE_OFF]
    mov [current_file_size], eax
    mov bx, [si + DIR_FIRST_CLUSTER_OFF]
    test eax, eax
    jnz .load
    clc
    ret

.load:
    call load_cluster_chain_to_buffer
    jc .fail
    mov eax, [current_file_size]
    clc
    ret

.fail:
    stc
    ret

load_cluster_chain_to_buffer:
    push ebx
    push ecx
    push edx
    push esi

    xor edx, edx

.cluster_loop:
    cmp bx, 2
    jb .done
    cmp bx, 0xFFF8
    jae .done

    mov ax, bx
    call cluster_to_abs_lba
    xor cx, cx
    mov cl, [sectors_per_cluster]
    push bx
    call read_sector_count_to_phys
    pop bx
    jc .fail

    mov eax, [cluster_size_bytes]
    add edx, eax

    mov ax, bx
    call fat16_next_cluster
    mov bx, ax
    jmp .cluster_loop

.done:
    mov eax, edx
    clc
    jmp .out

.fail:
    stc

.out:
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

cluster_to_abs_lba:
    movzx eax, ax
    sub eax, 2
    movzx ecx, byte [sectors_per_cluster]
    imul eax, ecx
    add eax, [data_lba_rel]
    add eax, [partition_lba_start]
    ret

fat16_next_cluster:
    push bx
    push es
    mov bx, FAT_BUFFER >> 4
    mov es, bx
    mov bx, ax
    shl bx, 1
    mov ax, [es:bx]
    pop es
    pop bx
    ret

read_sector_count_to_phys:
    push eax
    push bx
    push cx
    push dx

.sector_loop:
    test cx, cx
    jz .done
    push eax
    push cx
    call read_sector_to_phys
    pop cx
    pop eax
    jc .fail
    inc eax
    movzx edx, word [bytes_per_sector]
    add edi, edx
    dec cx
    jmp .sector_loop

.done:
    clc
    jmp .out

.fail:
    stc

.out:
    pop dx
    pop cx
    pop bx
    pop eax
    ret

read_sector_to_phys:
    push bx
    push dx
    push es

    call phys_to_esbx
    mov word [disk_packet.count], 1
    mov [disk_packet.offset], bx
    mov [disk_packet.segment], es
    mov [disk_packet.lba_low], eax
    mov dword [disk_packet.lba_high], 0
    mov dl, [boot_drive]
    mov si, disk_packet
    mov ah, 0x42
    int 0x13

    pop es
    pop dx
    pop bx
    ret

phys_to_esbx:
    push eax
    mov eax, edi
    mov bx, ax
    and bx, 0x000F
    shr eax, 4
    mov es, ax
    pop eax
    ret

serial_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret

serial_write_char:
    push dx
    push ax
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    pop ax
    out dx, al
    pop dx
    ret

puts16:
    push si
    push ax

.next:
    lodsb
    test al, al
    jz .done
    push ax
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    pop ax
    call serial_write_char
    jmp .next

.done:
    pop ax
    pop si
    ret

putc16:
    push ax
    push bx

    mov ah, 0x0E
    mov bh, 0
    int 0x10

    pop bx
    pop ax
    call serial_write_char
    ret

newline16:
    push ax
    mov al, 0x0D
    call putc16
    mov al, 0x0A
    call putc16
    pop ax
    ret

print_name_8dot3:
    push ax
    push bx
    push cx
    push di

    mov bx, di
    mov cx, 8

.name_loop:
    mov al, [es:bx]
    cmp al, ' '
    je .check_extension
    call putc16
    inc bx
    loop .name_loop

.check_extension:
    mov bx, di
    add bx, 8
    mov cx, 3

.scan_extension:
    mov al, [es:bx]
    cmp al, ' '
    jne .has_extension
    inc bx
    loop .scan_extension
    jmp .done

.has_extension:
    mov al, '.'
    call putc16

    mov bx, di
    add bx, 8
    mov cx, 3

.extension_loop:
    mov al, [es:bx]
    cmp al, ' '
    je .done
    call putc16
    inc bx
    loop .extension_loop

.done:
    pop di
    pop cx
    pop bx
    pop ax
    ret

print_root_listing:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov ax, ROOT_DIR_BUFFER >> 4
    mov es, ax
    xor di, di
    xor dx, dx
    xor bx, bx

.next_entry:
    cmp dx, [dir_entry_count]
    jae .done
    cmp bx, 5
    jae .done

    mov al, [es:di]
    cmp al, 0
    je .done
    cmp al, 0xE5
    je .skip_entry

    mov al, [es:di + DIR_ATTRIBUTES_OFF]
    cmp al, DIR_ATTR_LFN
    je .skip_entry
    test al, DIR_ATTR_VOLUME_ID
    jnz .skip_entry

    mov al, ' '
    call putc16
    mov al, ' '
    call putc16
    call print_name_8dot3

    mov al, [es:di + DIR_ATTRIBUTES_OFF]
    test al, DIR_ATTR_DIRECTORY
    jz .finish_entry

    mov si, msg_dir_suffix
    call puts16

.finish_entry:
    call newline16
    inc bx

.skip_entry:
    add di, 32
    inc dx
    jmp .next_entry

.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

print_demo_file:
    push eax
    push ecx
    push di
    push es

    mov ax, DEMO_FILE_BUFFER >> 4
    mov es, ax
    xor di, di
    mov ecx, [demo_file_size]
    test ecx, ecx
    jz .done

.next_char:
    mov al, [es:di]
    call putc16
    inc di
    dec ecx
    jnz .next_char

.done:
    call newline16
    pop es
    pop di
    pop ecx
    pop eax
    ret

print_boot_demo:
    push si

    mov si, msg_kernel_up
    call puts16
    mov si, msg_root_listing
    call puts16
    call print_root_listing
    mov si, msg_demo_listing
    call puts16
    call print_demo_file
    mov si, msg_system_halted
    call puts16

    pop si
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

[BITS 32]
protected_mode_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, PM_STACK_TOP

    mov esi, KERNEL_TEMP_ADDR
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, [STAGE2_LOAD_ADDR + kernel_size]
    add ecx, 3
    shr ecx, 2
    cld
    rep movsd

    mov dword [BOOTINFO_ADDR + BOOTINFO_MAGIC_OFF], BOOTINFO_MAGIC
    mov al, [STAGE2_LOAD_ADDR + boot_drive]
    mov [BOOTINFO_ADDR + BOOTINFO_BOOT_DRIVE_OFF], al
    mov al, [STAGE2_LOAD_ADDR + boot_partition_index]
    mov [BOOTINFO_ADDR + BOOTINFO_PARTITION_INDEX_OFF], al
    mov ax, [STAGE2_LOAD_ADDR + bytes_per_sector]
    mov [BOOTINFO_ADDR + BOOTINFO_BYTES_PER_SECTOR_OFF], ax
    mov eax, [STAGE2_LOAD_ADDR + partition_lba_start]
    mov [BOOTINFO_ADDR + BOOTINFO_PARTITION_LBA_OFF], eax
    mov eax, [STAGE2_LOAD_ADDR + partition_sector_count]
    mov [BOOTINFO_ADDR + BOOTINFO_PARTITION_COUNT_OFF], eax
    mov word [BOOTINFO_ADDR + BOOTINFO_SCREEN_COLS_OFF], 80
    mov word [BOOTINFO_ADDR + BOOTINFO_SCREEN_ROWS_OFF], 25

    mov eax, BOOTINFO_ADDR
    mov ebx, KERNEL_LOAD_ADDR
    jmp ebx

gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd STAGE2_LOAD_ADDR + gdt_start

CODE_SEL equ 0x08
DATA_SEL equ 0x10

[BITS 16]
msg_stage2:          db 'Stage2: starting', ENDL, 0
msg_boot_ok:         db 'Stage2: boot sector loaded', ENDL, 0
msg_parse_ok:        db 'Stage2: BPB parsed', ENDL, 0
msg_root:            db 'Stage2: loading FAT16 metadata', ENDL, 0
msg_pm:              db 'Stage2: entering protected mode', ENDL, 0
msg_boot_error:      db 'Stage2: boot sector read failed', ENDL, 0
msg_root_error:      db 'Stage2: root directory read failed', ENDL, 0
msg_fat_error:       db 'Stage2: FAT read failed', ENDL, 0
msg_kernel_missing:  db 'Stage2: KERNEL.BIN not found', ENDL, 0
msg_kernel_read:     db 'Stage2: kernel read failed', ENDL, 0
msg_mydir_missing:   db 'Stage2: MYDIR not found', ENDL, 0
msg_demo_missing:    db 'Stage2: TEST.TXT not found', ENDL, 0
msg_demo_read:       db 'Stage2: demo file read failed', ENDL, 0
msg_kernel_up:       db 'FunnyOS kernel up', ENDL, 0
msg_root_listing:    db 'Root directory:', ENDL, 0
msg_demo_listing:    db 'Demo file:', ENDL, 0
msg_system_halted:   db 'System halted.', ENDL, 0
msg_dir_suffix:      db ' <DIR>', 0

kernel_file_name:    db 'KERNEL  BIN'
mydir_name:          db 'MYDIR      '
test_file_name:      db 'TEST    TXT'

boot_drive:          db 0
boot_partition_index: db 0
fat_count:           db 0
sectors_per_cluster: db 0
bytes_per_sector:    dw 0
reserved_sectors:    dw 0
dir_entry_count:     dw 0
sectors_per_fat:     dw 0
subdir_entry_count:  dw 0

hidden_sectors:      dd 0
total_sectors:       dd 0
partition_lba_start: dd 0
partition_sector_count: dd 0
root_dir_sectors:    dd 0
root_dir_lba_rel:    dd 0
data_lba_rel:        dd 0
cluster_size_bytes:  dd 0
kernel_size:         dd 0
demo_file_size:      dd 0
current_file_size:   dd 0

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

found_entry:         times 32 db 0
