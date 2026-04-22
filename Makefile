ASM ?= nasm
HOST_CC ?= gcc
QEMU ?= qemu-system-x86_64
HDIUTIL ?= hdiutil
MFORMAT ?= mformat
MCOPY ?= mcopy
MMD ?= mmd

CROSS_PREFIX ?= $(shell if command -v x86_64-elf-gcc >/dev/null 2>&1; then printf 'x86_64-elf-'; fi)
CROSS_GCC := $(CROSS_PREFIX)gcc
CROSS_LD := $(CROSS_PREFIX)ld
CROSS_OBJCOPY := $(CROSS_PREFIX)objcopy
UEFI_PREFIX ?= $(shell if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then printf 'x86_64-w64-mingw32-'; fi)
UEFI_GCC := $(UEFI_PREFIX)gcc

BUILD_DIR := build
DISK_IMAGE := $(BUILD_DIR)/funnyos.img
ESP_DIR := $(BUILD_DIR)/esp
ESP_READY := $(BUILD_DIR)/.esp-ready
DISK_IMAGE_SIZE_MB := 128
SRC_DIR := src
UEFI_BOOT_SRC := $(SRC_DIR)/boot/uefi/boot.c
KERNEL_LD_SCRIPT := $(SRC_DIR)/kernel/linker.ld
PROGRAM_LD_SCRIPT := $(SRC_DIR)/programs/common/linker.ld

OVMF_CODE ?= $(shell for file in /opt/homebrew/share/qemu/edk2-x86_64-code.fd /opt/homebrew/share/qemu/OVMF_CODE.fd /usr/local/share/qemu/edk2-x86_64-code.fd /usr/local/share/qemu/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/x64/OVMF_CODE.fd /usr/share/qemu/OVMF_CODE.fd /usr/share/qemu/edk2-x86_64-code.fd; do [ -f "$$file" ] && { printf '%s' "$$file"; break; }; done)
OVMF_VARS ?= $(shell for file in /opt/homebrew/share/qemu/edk2-x86_64-vars.fd /opt/homebrew/share/qemu/edk2-i386-vars.fd /opt/homebrew/share/qemu/OVMF_VARS.fd /usr/local/share/qemu/edk2-x86_64-vars.fd /usr/local/share/qemu/edk2-i386-vars.fd /usr/local/share/qemu/OVMF_VARS.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/edk2/x64/OVMF_VARS.fd /usr/share/qemu/OVMF_VARS.fd /usr/share/qemu/edk2-x86_64-vars.fd /usr/share/qemu/edk2-i386-vars.fd; do [ -f "$$file" ] && { printf '%s' "$$file"; break; }; done)

KERNEL_C_SRCS := $(wildcard $(SRC_DIR)/kernel/*.c)
KERNEL_ASM_SRCS := $(wildcard $(SRC_DIR)/kernel/*.asm)
KERNEL_C_OBJS := $(patsubst $(SRC_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst $(SRC_DIR)/kernel/%.asm,$(BUILD_DIR)/kernel/%.o,$(KERNEL_ASM_SRCS))

PROGRAM_COMMON_C_SRCS := $(SRC_DIR)/programs/common/runtime.c
PROGRAM_COMMON_ASM_SRCS := $(SRC_DIR)/programs/common/entry.asm
HELLO_PROGRAM_SRC := $(SRC_DIR)/programs/hello/main.c
ARGS_PROGRAM_SRC := $(SRC_DIR)/programs/args/main.c
PROGRAM_COMMON_C_OBJS := $(patsubst $(SRC_DIR)/programs/common/%.c,$(BUILD_DIR)/programs/common/%.o,$(PROGRAM_COMMON_C_SRCS))
PROGRAM_COMMON_ASM_OBJS := $(patsubst $(SRC_DIR)/programs/common/%.asm,$(BUILD_DIR)/programs/common/%.o,$(PROGRAM_COMMON_ASM_SRCS))
HELLO_PROGRAM_OBJ := $(BUILD_DIR)/programs/hello/main.o
ARGS_PROGRAM_OBJ := $(BUILD_DIR)/programs/args/main.o
HELLO_PROGRAM_ELF := $(BUILD_DIR)/programs/HELLO.ELF
ARGS_PROGRAM_ELF := $(BUILD_DIR)/programs/ARGS.ELF

PATH_TEST_TOOL := $(BUILD_DIR)/tools/path_test
ROOT_TEST_FILE := test.txt
DEMO_TEST_FILE := test.txt

UEFI_BOOT_CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fshort-wchar -m64 -mno-red-zone -Wall -Wextra -Werror -std=c11 -I$(SRC_DIR)/common -I$(SRC_DIR)/boot/uefi
KERNEL_CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror -std=c11 -I$(SRC_DIR)/common -I$(SRC_DIR)/kernel
PROGRAM_CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -m64 -mno-red-zone -mcmodel=large -Wall -Wextra -Werror -std=c11 -I$(SRC_DIR)/common -I$(SRC_DIR)/programs/common

.PHONY: all image run run-headless run-window run-ahci run-ahci-headless run-ahci-window debug debug-headless debug-ahci debug-ahci-headless test clean check-build-tools check-run-tools

all: image

image: check-build-tools $(DISK_IMAGE)

run: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -drive format=raw,file=$(DISK_IMAGE),if=ide -serial none -monitor none

run-headless: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -drive format=raw,file=$(DISK_IMAGE),if=ide -display none -serial stdio -monitor none

run-window: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -drive format=raw,file=$(DISK_IMAGE),if=ide -serial none -monitor none

run-ahci: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -machine q35 -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -device ich9-ahci,id=ahci -drive id=disk,file=$(DISK_IMAGE),if=none,format=raw -device ide-hd,drive=disk,bus=ahci.0 -serial none -monitor none

run-ahci-headless: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -machine q35 -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -device ich9-ahci,id=ahci -drive id=disk,file=$(DISK_IMAGE),if=none,format=raw -device ide-hd,drive=disk,bus=ahci.0 -display none -serial stdio -monitor none

run-ahci-window: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -machine q35 -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -device ich9-ahci,id=ahci -drive id=disk,file=$(DISK_IMAGE),if=none,format=raw -device ide-hd,drive=disk,bus=ahci.0 -serial none -monitor none

debug: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -drive format=raw,file=$(DISK_IMAGE),if=ide -serial stdio -monitor none -s -S

debug-headless: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -drive format=raw,file=$(DISK_IMAGE),if=ide -display none -serial stdio -monitor none -s -S

debug-ahci: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -machine q35 -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -device ich9-ahci,id=ahci -drive id=disk,file=$(DISK_IMAGE),if=none,format=raw -device ide-hd,drive=disk,bus=ahci.0 -serial stdio -monitor none -s -S

debug-ahci-headless: check-run-tools image $(BUILD_DIR)/ovmf-vars.fd
	$(QEMU) -machine q35 -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(BUILD_DIR)/ovmf-vars.fd -device ich9-ahci,id=ahci -drive id=disk,file=$(DISK_IMAGE),if=none,format=raw -device ide-hd,drive=disk,bus=ahci.0 -display none -serial stdio -monitor none -s -S

test: image $(PATH_TEST_TOOL)
	$(PATH_TEST_TOOL)

check-build-tools:
	@command -v $(ASM) >/dev/null || { echo "Missing tool: $(ASM)"; exit 1; }
	@command -v $(HOST_CC) >/dev/null || { echo "Missing tool: $(HOST_CC)"; exit 1; }
	@test -n "$(CROSS_PREFIX)" || { echo "Missing cross toolchain: install x86_64-elf-gcc"; exit 1; }
	@command -v $(CROSS_GCC) >/dev/null || { echo "Missing tool: $(CROSS_GCC)"; exit 1; }
	@command -v $(CROSS_LD) >/dev/null || { echo "Missing tool: $(CROSS_LD)"; exit 1; }
	@command -v $(CROSS_OBJCOPY) >/dev/null || { echo "Missing tool: $(CROSS_OBJCOPY)"; exit 1; }
	@test -n "$(UEFI_PREFIX)" || { echo "Missing UEFI toolchain: install x86_64-w64-mingw32-gcc"; exit 1; }
	@command -v $(UEFI_GCC) >/dev/null || { echo "Missing tool: $(UEFI_GCC)"; exit 1; }
	@test -n "$(OVMF_CODE)" || { echo "Missing OVMF code image"; exit 1; }
	@test -n "$(OVMF_VARS)" || { echo "Missing OVMF vars image"; exit 1; }
	@if command -v $(HDIUTIL) >/dev/null 2>&1; then :; \
	elif command -v $(MFORMAT) >/dev/null 2>&1 && command -v $(MCOPY) >/dev/null 2>&1 && command -v $(MMD) >/dev/null 2>&1; then :; \
	else echo "Missing disk image tools: need $(HDIUTIL) or mtools ($(MFORMAT), $(MCOPY), $(MMD))"; exit 1; fi

check-run-tools: check-build-tools
	@command -v $(QEMU) >/dev/null || { echo "Missing tool: $(QEMU)"; exit 1; }

$(ESP_READY): Makefile $(BUILD_DIR)/EFI/BOOT/BOOTX64.EFI $(BUILD_DIR)/kernel.elf $(HELLO_PROGRAM_ELF) $(ARGS_PROGRAM_ELF) $(ROOT_TEST_FILE) $(DEMO_TEST_FILE) | $(BUILD_DIR)
	rm -rf $(ESP_DIR)
	mkdir -p $(ESP_DIR)/EFI/BOOT $(ESP_DIR)/MYDIR $(ESP_DIR)/BIGDIR
	cp $(BUILD_DIR)/EFI/BOOT/BOOTX64.EFI $(ESP_DIR)/EFI/BOOT/BOOTX64.EFI
	cp $(BUILD_DIR)/kernel.elf $(ESP_DIR)/KERNEL.ELF
	cp $(HELLO_PROGRAM_ELF) $(ESP_DIR)/HELLO.ELF
	cp $(ARGS_PROGRAM_ELF) $(ESP_DIR)/ARGS.ELF
	cp $(ROOT_TEST_FILE) $(ESP_DIR)/TEST.TXT
	cp $(DEMO_TEST_FILE) $(ESP_DIR)/MYDIR/TEST.TXT
	printf 'fs0:\\EFI\\BOOT\\BOOTX64.EFI\r\n' > $(ESP_DIR)/startup.nsh
	: > $(ESP_DIR)/BIGDIR/ITEM69.TXT
	python3 -c 'from pathlib import Path; root = Path("$(ESP_DIR)"); (root / "BIGFILE.TXT").write_text("FunnyOS big file line for FAT32 multi-cluster testing.\\n" * 80); [(root / "BIGDIR" / f"ITEM{i:02d}.TXT").write_text("") for i in range(70)]'
	if command -v xattr >/dev/null 2>&1; then xattr -cr $(ESP_DIR) >/dev/null 2>&1 || echo "warning: xattr cleanup failed for $(ESP_DIR); continuing"; fi
	touch $@

$(DISK_IMAGE): Makefile $(ESP_READY) | $(BUILD_DIR)
	rm -f $@ $(BUILD_DIR)/funnyos-stage.dmg
	@if command -v $(HDIUTIL) >/dev/null 2>&1; then \
		mount_dir=$$(mktemp -d /tmp/funnyos-mnt.XXXXXX) && \
		$(HDIUTIL) create -quiet -ov -size $(DISK_IMAGE_SIZE_MB)m -fs "MS-DOS FAT32" -volname FUNNYOS $(BUILD_DIR)/funnyos-stage.dmg && \
		$(HDIUTIL) attach -quiet -nobrowse -mountpoint "$$mount_dir" $(BUILD_DIR)/funnyos-stage.dmg && \
		COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 cp -X -R $(ESP_DIR)/. "$$mount_dir"/ && \
		if command -v dot_clean >/dev/null 2>&1; then dot_clean -m "$$mount_dir" >/dev/null 2>&1 || true; fi && \
		find "$$mount_dir" \( -name '._*' -o -name '.DS_Store' \) -delete && \
		sync && \
		$(HDIUTIL) detach -quiet "$$mount_dir" && \
		rmdir "$$mount_dir" && \
		mv $(BUILD_DIR)/funnyos-stage.dmg $@; \
	else \
		dd if=/dev/zero of=$@ bs=1m count=0 seek=$(DISK_IMAGE_SIZE_MB) status=none && \
		$(MFORMAT) -i $@ -F -v FUNNYOS :: && \
		$(MMD) -i $@ ::/EFI ::/EFI/BOOT ::/MYDIR ::/BIGDIR && \
		$(MCOPY) -i $@ $(ESP_DIR)/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI && \
		$(MCOPY) -i $@ $(ESP_DIR)/KERNEL.ELF ::/KERNEL.ELF && \
		$(MCOPY) -i $@ $(ESP_DIR)/HELLO.ELF ::/HELLO.ELF && \
		$(MCOPY) -i $@ $(ESP_DIR)/ARGS.ELF ::/ARGS.ELF && \
		$(MCOPY) -i $@ $(ESP_DIR)/TEST.TXT ::/TEST.TXT && \
		$(MCOPY) -i $@ $(ESP_DIR)/startup.nsh ::/startup.nsh && \
		$(MCOPY) -i $@ $(ESP_DIR)/BIGFILE.TXT ::/BIGFILE.TXT && \
		$(MCOPY) -i $@ $(ESP_DIR)/MYDIR/TEST.TXT ::/MYDIR/TEST.TXT && \
		$(MCOPY) -s -i $@ $(ESP_DIR)/BIGDIR/* ::/BIGDIR/; \
	fi

$(BUILD_DIR)/ovmf-vars.fd: $(OVMF_VARS) | $(BUILD_DIR)
	cp $< $@

$(BUILD_DIR)/EFI/BOOT/BOOTX64.EFI: $(BUILD_DIR)/boot/uefi/boot.efi | $(BUILD_DIR)/EFI/BOOT
	cp $< $@

$(BUILD_DIR)/boot/uefi/boot.efi: $(BUILD_DIR)/boot/uefi/boot.o | $(BUILD_DIR)/boot/uefi
	$(UEFI_GCC) -nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main -Wl,--image-base,0x100000 -Wl,--file-alignment,512 -Wl,--section-alignment,4096 $< -o $@
	$(CROSS_OBJCOPY) --remove-section .comment --remove-section .eh_fram --remove-section .idata $@

$(BUILD_DIR)/boot/uefi/boot.o: $(UEFI_BOOT_SRC) | $(BUILD_DIR)/boot/uefi
	$(UEFI_GCC) $(UEFI_BOOT_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS) $(KERNEL_LD_SCRIPT)
	$(CROSS_LD) -T $(KERNEL_LD_SCRIPT) -o $@ $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

$(HELLO_PROGRAM_ELF): $(PROGRAM_COMMON_C_OBJS) $(PROGRAM_COMMON_ASM_OBJS) $(HELLO_PROGRAM_OBJ) $(PROGRAM_LD_SCRIPT)
	$(CROSS_LD) -T $(PROGRAM_LD_SCRIPT) -o $@ $(PROGRAM_COMMON_ASM_OBJS) $(PROGRAM_COMMON_C_OBJS) $(HELLO_PROGRAM_OBJ)

$(ARGS_PROGRAM_ELF): $(PROGRAM_COMMON_C_OBJS) $(PROGRAM_COMMON_ASM_OBJS) $(ARGS_PROGRAM_OBJ) $(PROGRAM_LD_SCRIPT)
	$(CROSS_LD) -T $(PROGRAM_LD_SCRIPT) -o $@ $(PROGRAM_COMMON_ASM_OBJS) $(PROGRAM_COMMON_C_OBJS) $(ARGS_PROGRAM_OBJ)

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c | $(BUILD_DIR)/kernel
	$(CROSS_GCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.asm | $(BUILD_DIR)/kernel
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/programs/common/%.o: $(SRC_DIR)/programs/common/%.c | $(BUILD_DIR)/programs/common
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/programs/common/%.o: $(SRC_DIR)/programs/common/%.asm | $(BUILD_DIR)/programs/common
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/programs/hello/%.o: $(SRC_DIR)/programs/hello/%.c | $(BUILD_DIR)/programs/hello
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/programs/args/%.o: $(SRC_DIR)/programs/args/%.c | $(BUILD_DIR)/programs/args
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(PATH_TEST_TOOL): tests/path_test.c $(SRC_DIR)/kernel/path.c $(SRC_DIR)/kernel/kstring.c | $(BUILD_DIR)/tools
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I$(SRC_DIR)/common -I$(SRC_DIR)/kernel tests/path_test.c $(SRC_DIR)/kernel/path.c $(SRC_DIR)/kernel/kstring.c -o $@

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot:
	mkdir -p $@

$(BUILD_DIR)/boot/uefi:
	mkdir -p $@

$(BUILD_DIR)/kernel:
	mkdir -p $@

$(BUILD_DIR)/programs:
	mkdir -p $@

$(BUILD_DIR)/programs/common:
	mkdir -p $@

$(BUILD_DIR)/programs/hello:
	mkdir -p $@

$(BUILD_DIR)/programs/args:
	mkdir -p $@

$(BUILD_DIR)/tools:
	mkdir -p $@

$(BUILD_DIR)/EFI:
	mkdir -p $@

$(BUILD_DIR)/EFI/BOOT:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
