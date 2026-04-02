[ORG 0x7C00]
[BITS 16]

%define ENDL 0x0D, 0x0A
%define HANDOFF_ADDR 0x0600
%define VBR_TEMP_ADDR 0x7A00
%define COPY_STUB_ADDR 0x0500

jmp start
nop

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [HANDOFF_ADDR + 0], dl

    mov si, 0x7DBE
    xor bx, bx

.scan_partition:
    cmp byte [si], 0x80
    je .found_partition
    add si, 16
    inc bl
    cmp bl, 4
    jb .scan_partition
    mov si, msg_no_active
    jmp fatal

.found_partition:
    mov [HANDOFF_ADDR + 1], bl
    mov eax, [si + 8]
    mov [HANDOFF_ADDR + 4], eax
    mov eax, [si + 12]
    mov [HANDOFF_ADDR + 8], eax

    mov word [dap.count], 1
    mov word [dap.offset], VBR_TEMP_ADDR
    mov word [dap.segment], 0
    mov eax, [HANDOFF_ADDR + 4]
    mov [dap.lba_low], eax
    mov dword [dap.lba_high], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [HANDOFF_ADDR + 0]
    int 0x13
    jc .read_error

    mov si, copy_stub
    mov di, COPY_STUB_ADDR
    mov cx, copy_stub_end - copy_stub
    rep movsb
    jmp 0x0000:COPY_STUB_ADDR

.read_error:
    mov si, msg_read_failed
    jmp fatal

fatal:
    call puts
.halt:
    cli
    hlt
    jmp .halt

puts:
    push ax
    push si

.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .next

.done:
    pop si
    pop ax
    ret

msg_no_active:   db 'No active partition', ENDL, 0
msg_read_failed: db 'VBR read failed', ENDL, 0

copy_stub:
    cld
    mov si, VBR_TEMP_ADDR
    mov di, 0x7C00
    mov cx, 256
    rep movsw
    mov dl, [HANDOFF_ADDR + 0]
    jmp 0x0000:0x7C00
copy_stub_end:

dap:
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
