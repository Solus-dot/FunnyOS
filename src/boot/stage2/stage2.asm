[ORG 0]
[BITS 16]

%define ENDL 0x0D, 0x0A

%define BOOT_SECTOR_BUFFER  0x05000
%define FAT_BUFFER          0x06000
%define ROOT_DIR_BUFFER     0x08000
%define SUBDIR_BUFFER       0x0A000
%define DEMO_FILE_BUFFER    0x0B000
%define BOOTINFO_ADDR       0x0C000
%define STAGE2_LOAD_ADDR    0x00020000
%define KERNEL_TEMP_ADDR    0x30000
%define KERNEL_LOAD_ADDR    0x00100000
%define RM_STACK_SEGMENT    0x7000
%define PM_STACK_TOP        0x00090000

%define BOOTINFO_MAGIC                  0x534F4E46
%define BOOTINFO_MAGIC_OFF              0
%define BOOTINFO_BOOT_DRIVE_OFF         4
%define BOOTINFO_BOOT_DEVICE_TYPE_OFF   6
%define BOOTINFO_MEMORY_MAP_ADDR_OFF    8
%define BOOTINFO_MEMORY_MAP_COUNT_OFF   12
%define BOOTINFO_ROOT_ADDR_OFF          16
%define BOOTINFO_ROOT_COUNT_OFF         20
%define BOOTINFO_DEMO_ADDR_OFF          24
%define BOOTINFO_DEMO_SIZE_OFF          28
%define BOOTINFO_SCREEN_COLS_OFF        32
%define BOOTINFO_SCREEN_ROWS_OFF        34

%define DIR_ATTR_DIRECTORY 0x10
%define DIR_ATTR_VOLUME_ID 0x08
%define DIR_ATTR_LFN       0x0F

%define BPB_BYTES_PER_SECTOR_OFF     11
%define BPB_SECTORS_PER_CLUSTER_OFF  13
%define BPB_RESERVED_SECTORS_OFF     14
%define BPB_FAT_COUNT_OFF            16
%define BPB_DIR_ENTRY_COUNT_OFF      17
%define BPB_TOTAL_SECTORS_OFF        19
%define BPB_SECTORS_PER_FAT_OFF      22
%define BPB_SECTORS_PER_TRACK_OFF    24
%define BPB_HEADS_OFF                26

%define DIR_NAME_OFF             0
%define DIR_ATTRIBUTES_OFF       11
%define DIR_FIRST_CLUSTER_OFF    26
%define DIR_FILE_SIZE_OFF        28

start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ax, RM_STACK_SEGMENT
    mov ss, ax
    mov sp, 0xFFF0
    sti

    mov [boot_drive], dl

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

    mov si, mydir_name
    mov di, found_entry
    call find_root_entry
    jc mydir_not_found

    mov al, [found_entry + DIR_ATTRIBUTES_OFF]
    test al, DIR_ATTR_DIRECTORY
    jz mydir_not_found

    mov si, found_entry
    call load_directory_cluster
    jc mydir_not_found

    mov si, test_file_name
    mov di, found_entry
    call find_subdir_entry
    jc demo_not_found

    mov si, found_entry
    mov edi, DEMO_FILE_BUFFER
    call load_file_from_entry
    jc demo_read_error
    mov [demo_file_size], eax

    mov si, msg_pm
    call puts16
    call print_boot_demo

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

mydir_not_found:
    mov si, msg_mydir_missing
    jmp fatal16

demo_not_found:
    mov si, msg_demo_missing
    jmp fatal16

demo_read_error:
    mov si, msg_demo_read
    jmp fatal16

fatal16:
    call puts16
.halt:
    cli
    hlt
    jmp .halt

read_boot_sector:
    clc
    ret

parse_boot_sector:
    mov ax, 512
    mov [bytes_per_sector], ax
    mov al, 1
    mov [sectors_per_cluster], al
    mov ax, 1
    mov [reserved_sectors], ax
    mov al, 2
    mov [fat_count], al
    mov ax, 224
    mov [dir_entry_count], ax
    mov ax, 9
    mov [sectors_per_fat], ax
    mov ax, 18
    mov [sectors_per_track], ax
    mov ax, 2
    mov [heads], ax

    mov ax, [dir_entry_count]
    shl ax, 5
    xor dx, dx
    div word [bytes_per_sector]
    test dx, dx
    jz .root_ready
    inc ax

.root_ready:
    mov [root_dir_sectors], ax

    mov ax, [sectors_per_fat]
    xor bx, bx
    mov bl, [fat_count]
    mul bx
    add ax, [reserved_sectors]
    mov [root_dir_lba], ax
    add ax, [root_dir_sectors]
    mov [data_lba], ax

    ret

read_root_directory:
    xor eax, eax
    mov ax, [root_dir_lba]
    mov edi, ROOT_DIR_BUFFER
    mov cx, [root_dir_sectors]
    call read_sector_count_to_phys
    ret

read_fat_table:
    xor eax, eax
    mov ax, [reserved_sectors]
    mov edi, FAT_BUFFER
    mov cx, [sectors_per_fat]
    call read_sector_count_to_phys
    ret

find_root_entry:
    mov bx, [dir_entry_count]
    mov dx, ROOT_DIR_BUFFER >> 4
    jmp find_entry_in_segment

find_subdir_entry:
    xor bx, bx
    mov bl, [sectors_per_cluster]
    shl bx, 4
    mov dx, SUBDIR_BUFFER >> 4

find_entry_in_segment:
    push es
    push bp
    mov bp, di
    mov es, dx
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

    push cx
    push si
    mov di, dx
    mov cx, 11
    repe cmpsb
    pop si
    pop cx
    je .found
    jmp .advance

.found:
    push ds
    mov ax, es
    mov ds, ax
    pop es
    mov si, dx
    mov di, bp
    mov cx, 16
    rep movsw
    mov ax, cs
    mov ds, ax
    pop es
    pop bp
    clc
    ret

.advance:
    add dx, 32
    inc cx
    jmp .next_entry

.not_found:
    pop es
    pop bp
    stc
    ret

load_directory_cluster:
    push eax
    mov ax, [si + DIR_FIRST_CLUSTER_OFF]
    call cluster_to_lba
    mov edi, SUBDIR_BUFFER
    xor cx, cx
    mov cl, [sectors_per_cluster]
    call read_sector_count_to_phys
    pop eax
    ret

load_file_from_entry:
    push ebx
    push ecx
    push edx
    push esi

    mov eax, [si + DIR_FILE_SIZE_OFF]
    mov [current_file_size], eax
    mov bx, [si + DIR_FIRST_CLUSTER_OFF]
    xor edx, edx

.cluster_loop:
    cmp bx, 0x0FF8
    jae .done

    mov ax, bx
    call cluster_to_lba
    xor cx, cx
    mov cl, [sectors_per_cluster]
    call read_sector_count_to_phys
    jc .fail

    movzx eax, byte [sectors_per_cluster]
    movzx ecx, word [bytes_per_sector]
    mul ecx
    add edi, eax

    mov ax, bx
    call fat_next_cluster
    mov bx, ax
    jmp .cluster_loop

.done:
    mov eax, [current_file_size]
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

cluster_to_lba:
    sub ax, 2
    xor cx, cx
    mov cl, [sectors_per_cluster]
    mul cx
    add ax, [data_lba]
    ret

fat_next_cluster:
    push bx
    push dx
    push es
    mov bx, ax
    mov cx, 3
    mul cx
    mov cx, 2
    div cx
    mov bx, FAT_BUFFER >> 4
    mov es, bx
    mov bx, ax
    mov ax, [es:bx]
    test dx, dx
    jz .even
    shr ax, 4
    jmp .ready

.even:
    and ax, 0x0FFF

.ready:
    pop es
    pop dx
    pop bx
    ret

read_sector_count_to_phys:
    push ax
    push bx
    push cx
    push dx

.sector_loop:
    test cx, cx
    jz .done
    push ax
    push cx
    call read_sector_to_phys
    pop cx
    pop ax
    jc .fail
    inc ax
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
    pop ax
    ret

read_sector_to_phys:
    push bx
    push cx
    push dx
    push es

    call phys_to_esbx
    mov cl, 1
    mov dl, [boot_drive]
    call disk_read

    pop es
    pop dx
    pop cx
    pop bx
    ret

lba_to_chs:
    push ax

    xor dx, dx
    div word [sectors_per_track]
    inc dx
    mov cx, dx

    xor dx, dx
    div word [heads]
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah

    pop ax
    mov dl, [boot_drive]
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

disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    push cx
    call lba_to_chs
    pop ax

    mov ah, 0x02
    mov di, 3

.retry:
    pusha
    stc
    int 0x13
    jnc .success

    popa
    call disk_reset
    dec di
    test di, di
    jnz .retry
    stc
    jmp .done

.success:
    popa
    clc

.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

disk_reset:
    push ax
    mov ah, 0
    mov dl, [boot_drive]
    stc
    int 0x13
    pop ax
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
    push ax
    push cx
    push di
    push es

    mov ax, DEMO_FILE_BUFFER >> 4
    mov es, ax
    xor di, di
    mov cx, [demo_file_size]
    jcxz .done

.next_char:
    mov al, [es:di]
    call putc16
    inc di
    loop .next_char

.done:
    call newline16
    pop es
    pop di
    pop cx
    pop ax
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
    rep movsd

    mov dword [BOOTINFO_ADDR + BOOTINFO_MAGIC_OFF], BOOTINFO_MAGIC
    movzx eax, byte [STAGE2_LOAD_ADDR + boot_drive]
    mov word [BOOTINFO_ADDR + BOOTINFO_BOOT_DRIVE_OFF], ax
    movzx eax, byte [STAGE2_LOAD_ADDR + boot_device_type]
    mov word [BOOTINFO_ADDR + BOOTINFO_BOOT_DEVICE_TYPE_OFF], ax
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEMORY_MAP_ADDR_OFF], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_MEMORY_MAP_COUNT_OFF], 0
    mov dword [BOOTINFO_ADDR + BOOTINFO_ROOT_ADDR_OFF], ROOT_DIR_BUFFER
    movzx eax, word [STAGE2_LOAD_ADDR + dir_entry_count]
    mov dword [BOOTINFO_ADDR + BOOTINFO_ROOT_COUNT_OFF], eax
    mov dword [BOOTINFO_ADDR + BOOTINFO_DEMO_ADDR_OFF], DEMO_FILE_BUFFER
    mov eax, [STAGE2_LOAD_ADDR + demo_file_size]
    mov dword [BOOTINFO_ADDR + BOOTINFO_DEMO_SIZE_OFF], eax
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
msg_root:            db 'Stage2: loading FAT12 metadata', ENDL, 0
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
boot_device_type:    db 0
fat_count:           db 0
sectors_per_cluster: db 0
bytes_per_sector:    dw 0
reserved_sectors:    dw 0
dir_entry_count:     dw 0
sectors_per_fat:     dw 0
sectors_per_track:   dw 0
heads:               dw 0
root_dir_sectors:    dw 0
root_dir_lba:        dw 0
data_lba:            dw 0

kernel_size:         dd 0
demo_file_size:      dd 0
current_file_size:   dd 0

found_entry:         times 32 db 0
