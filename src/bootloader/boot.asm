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
ebr_volume_label:           db '  FunnyOS  '            ; 11 bytes, padded with spaces
ebr_system_id:              db 'FAT12   '               ; 8 bytes, padded with spaces

; CODE GOES HERE

start:
    ; Setup data segments
    mov ax, 0           ; Cannot write to ds/es directly\
    mov ds, ax
    mov es, ax

    ; Setup stack
    mov ss, ax
    mov sp, 0x7C00      ; stack grows downwards from where we loaded our memory

    ; Some BIOSes might start us at 7C00:0000 instead of 0000:7C00
    ; Make sure we are in the expected location
    push es
    push word .after
    retf

.after:

    ; Read something from the floppy disk
    ; BIOS should set DL to drive number
    mov [ebr_drive_number], dl

    ; Show loading message
    mov si, msg_loading
    call puts

    ; Read drive params (sectors per track and head count),
    ; Instead of relying on data on formatted disk
    push es
    mov ah, 0x08
    int 0x13
    jc floppy_error
    pop es

    and cl, 0x3F                        ; Remove top 2 bits
    xor ch, ch
    mov [bdb_sectors_per_track], cx     ; Sector count

    inc dh
    mov [bdb_heads], dh                 ; Head count

    ; Compute LBA of root dir = reserved + fats * sectors_per_fat
    ; Note: This section can be hardcoded
    mov ax, [bdb_sectors_per_fat]           
    mov bl, [bdb_fat_count]
    xor bh, bh
    mul bx                              ; dx:ax = (fats * sectors_per_fat)
    add ax, [bdb_reserved_sectors]      ; ax = LBA of root dir
    push ax

    ; Compute size of root dir = 32 * number_of_entries / bytes_per_sector
    mov ax, [bdb_dir_entries_count]
    shl ax, 5                           ; ax *= 32
    xor dx, dx                          ; dx = 0
    div word [bdb_bytes_per_sector]     ; Number of sectors we need to read

    test dx, dx                         ; If dx = 0, add 1
    jz .root_dir_after
    inc ax                              ; Division remainder != 0, add 1
                                        ; This means we have a sector only partially filled with entries

.root_dir_after:
    ; Read root dir
    mov cl, al                          ; cl = Number of sectors to read = size of root dir
    pop ax                              ; ax = LGA of root dir
    mov dl, [ebr_drive_number]          ; dl = Drive number (We saved it previously)
    mov bx, buffer                      ; es:bx = buffer
    call disk_read

    ; Search for kernel.bin
    xor bx, bx
    mov di, buffer

.search_kernel:
    mov si, file_kernel_bin
    mov cx, 11                          ; Compare upto 11 chars
    push di
    repe cmpsb                          ; cmpsb = Compare string bytes, compares 2 bytes located in memory at address ds:si and es:di
                                        ; si and di are incremented (when direction flag = 0) or decremented (when direction flag = 1)
                                        ; repe = Repeat an instruction while zero flag = 1 (operands are equal), or until cx = 0, cx-- each repetition
    pop di
    je .found_kernel

    add di, 32                          ; Move to next directory entry
    inc bx                              ; Increase directory entry count
    cmp bx, [bdb_dir_entries_count]
    jl .search_kernel

    ; Kernel not found
    jmp kernel_not_found_error

.found_kernel:

    ; di should still have the address of the entry
    mov ax, [di + 26]                   ; First logical cluster field (offset 26)
    mov [kernel_cluster], ax

    ; Load FAT from disk into memory
    mov ax, [bdb_reserved_sectors]
    mov bx, buffer
    mov cl, [bdb_sectors_per_fat]
    mov dl, [ebr_drive_number]
    call disk_read

    ; Read kernel and process FAT chain
    mov bx, KERNEL_LOAD_SEGMENT
    mov es, bx
    mov bx, KERNEL_LOAD_OFFSET

.load_kernel_loop:

    ; Read next cluster
    mov ax, [kernel_cluster]
    ; Hardcoded value, will need to change later
    add ax, 31                          ; First cluster = (kernel_cluster - 2) * sectors_per_cluster + start sector
                                        ; Start sector = reserved + fats + root dir size = 1 + 18 + 14 = 33
    mov cl, 1
    mov dl, [ebr_drive_number]
    call disk_read

    add bx, [bdb_bytes_per_sector]

    ; Compute location of next cluster
    mov ax, [kernel_cluster]
    mov cx, 3
    mul cx
    mov cx, 2
    div cx                              ; ax = Index of entry in FAT, dx = cluster % 2

    mov si, buffer
    add si, ax
    mov ax, [ds:si]                     ; Read entry from FAT table at index ax

    or dx, dx
    jz .even

.odd:
    shr ax, 4                           ; Right shift 4 bits
    jmp .next_cluster_after

.even:
    and ax, 0x0FFF

.next_cluster_after:
    cmp ax, 0x0FF8                      ; End of chain
    jae .read_finish

    mov [kernel_cluster], ax
    jmp .load_kernel_loop

.read_finish:

    ; Jump to our kernel
    mov dl, [ebr_drive_number]          ; Boot device in dl
    
    mov ax, KERNEL_LOAD_SEGMENT         ; Set segment registers
    mov ds, ax
    mov es, ax
    
    jmp KERNEL_LOAD_SEGMENT:KERNEL_LOAD_OFFSET 

    jmp wait_key_and_reboot             ; Should never happen
    

    cli                                 ; Disables interrupts so that CPU does not get out of halt state
    hlt                                 ; Stops CPU from executing

;
; ERROR HANDLERS
;

floppy_error:
    mov si, msg_read_failed
    call puts
    jmp wait_key_and_reboot

kernel_not_found_error:
    mov si, msg_kernel_not_found
    call puts
    jmp wait_key_and_reboot

wait_key_and_reboot:
    mov ah, 0
    int 0x16            ; Wait for keypress
    jmp 0FFFFh:0        ; Jump to beginning to BIOS, should reboot

.halt:
    cli                 ; Disables interrupts so that CPU does not get out of halt state
    hlt


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

    pop di                             ; Restore modified registers
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


msg_loading:            db 'Loading...', ENDL, 0
msg_read_failed:        db 'Read from disk failed!', ENDL, 0
msg_kernel_not_found:   db 'KERNEL.BIN file not found!', ENDL, 0
file_kernel_bin:        db 'KERNEL  BIN'
kernel_cluster:         dw 0

KERNEL_LOAD_SEGMENT     equ 0x2000
KERNEL_LOAD_OFFSET      equ 0

times 510-($-$$) db 0   ; Fill remaining space with zeros (510 bytes + the boot signature bytes 0x55 and 0xAA, ensuring 512 bytes in boot sector)
dw 0xAA55               ; boot signature

buffer: