ASM ?= nasm
HOST_CC ?= gcc
QEMU ?= qemu-system-x86_64

CROSS_PREFIX ?= $(shell if command -v x86_64-elf-gcc >/dev/null 2>&1; then printf 'x86_64-elf-'; fi)
CROSS_GCC := $(CROSS_PREFIX)gcc
CROSS_LD := $(CROSS_PREFIX)ld
CROSS_OBJCOPY := $(CROSS_PREFIX)objcopy
CROSS_SIZE := $(CROSS_PREFIX)size

BUILD_DIR := build
SRC_DIR := src

MBR_SRC := $(SRC_DIR)/boot/mbr/mbr.asm
STAGE1_SRC := $(SRC_DIR)/boot/stage1/boot.asm
STAGE2_SRC := $(SRC_DIR)/boot/stage2/stage2.asm
KERNEL_LD_SCRIPT := $(SRC_DIR)/kernel/linker.ld
PROGRAM_LD_SCRIPT := $(SRC_DIR)/programs/common/linker.ld

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
HELLO_PROGRAM_ELF := $(BUILD_DIR)/programs/HELLO.elf
HELLO_PROGRAM_PAYLOAD := $(BUILD_DIR)/programs/HELLO.payload
HELLO_PROGRAM_BIN := $(BUILD_DIR)/programs/HELLO.BIN
ARGS_PROGRAM_ELF := $(BUILD_DIR)/programs/ARGS.elf
ARGS_PROGRAM_PAYLOAD := $(BUILD_DIR)/programs/ARGS.payload
ARGS_PROGRAM_BIN := $(BUILD_DIR)/programs/ARGS.BIN

IMG_TOOL := $(BUILD_DIR)/tools/imgbuild
FAT_TOOL := $(BUILD_DIR)/tools/fat
PATH_TEST_TOOL := $(BUILD_DIR)/tools/path_test
PROGRAM_PACK_TOOL := $(BUILD_DIR)/tools/programpack

ROOT_TEST_FILE := test.txt
DEMO_TEST_FILE := test.txt

KERNEL_CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror -std=c11 -I$(SRC_DIR)/common -I$(SRC_DIR)/kernel
PROGRAM_CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector -m64 -mno-red-zone -mcmodel=small -Wall -Wextra -Werror -std=c11 -I$(SRC_DIR)/common -I$(SRC_DIR)/programs/common

.PHONY: all image run debug test clean check-build-tools check-run-tools

all: image

image: check-build-tools $(BUILD_DIR)/funnyos-disk.img

run: check-run-tools $(BUILD_DIR)/funnyos-disk.img
	$(QEMU) -drive format=raw,file=$(BUILD_DIR)/funnyos-disk.img,if=ide -serial stdio

debug: check-run-tools $(BUILD_DIR)/funnyos-disk.img
	$(QEMU) -drive format=raw,file=$(BUILD_DIR)/funnyos-disk.img,if=ide -serial stdio -monitor none -s -S

test: image $(FAT_TOOL) $(PATH_TEST_TOOL) $(PROGRAM_PACK_TOOL)
	$(PATH_TEST_TOOL)
	grep -F '#include "fs.h"' $(SRC_DIR)/kernel/shell.c >/dev/null
	if grep -F '#include "fat16.h"' $(SRC_DIR)/kernel/shell.c >/dev/null; then echo "shell still depends on fat16.h"; exit 1; fi
	if grep -F 'fat16_' $(SRC_DIR)/kernel/shell.c >/dev/null; then echo "shell still calls fat16 directly"; exit 1; fi
	sh tests/smoke.sh "$(BUILD_DIR)/funnyos-disk.img" "$(QEMU)" "$(IMG_TOOL)" "$(FAT_TOOL)" "$(BUILD_DIR)/mbr.bin" "$(BUILD_DIR)/stage1.bin" "$(BUILD_DIR)/stage2.bin" "$(BUILD_DIR)/kernel.bin" "$(ROOT_TEST_FILE)" "$(DEMO_TEST_FILE)" "$(HELLO_PROGRAM_BIN)" "$(ARGS_PROGRAM_BIN)" "$(PROGRAM_PACK_TOOL)"

check-build-tools:
	@command -v $(ASM) >/dev/null || { echo "Missing tool: $(ASM)"; exit 1; }
	@command -v $(HOST_CC) >/dev/null || { echo "Missing tool: $(HOST_CC)"; exit 1; }
	@test -n "$(CROSS_PREFIX)" || { echo "Missing cross toolchain: install x86_64-elf-gcc"; exit 1; }
	@command -v $(CROSS_GCC) >/dev/null || { echo "Missing tool: $(CROSS_GCC)"; exit 1; }
	@command -v $(CROSS_LD) >/dev/null || { echo "Missing tool: $(CROSS_LD)"; exit 1; }
	@command -v $(CROSS_OBJCOPY) >/dev/null || { echo "Missing tool: $(CROSS_OBJCOPY)"; exit 1; }
	@command -v $(CROSS_SIZE) >/dev/null || { echo "Missing tool: $(CROSS_SIZE)"; exit 1; }

check-run-tools: check-build-tools
	@command -v $(QEMU) >/dev/null || { echo "Missing tool: $(QEMU)"; exit 1; }

$(BUILD_DIR)/funnyos-disk.img: $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin $(HELLO_PROGRAM_BIN) $(ARGS_PROGRAM_BIN) $(IMG_TOOL) $(ROOT_TEST_FILE) $(DEMO_TEST_FILE)
	$(IMG_TOOL) $@ $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin $(ROOT_TEST_FILE) $(DEMO_TEST_FILE) $(HELLO_PROGRAM_BIN) $(ARGS_PROGRAM_BIN)

$(BUILD_DIR)/mbr.bin: $(MBR_SRC) | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/stage1.bin: $(STAGE1_SRC) | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/stage2.bin: $(STAGE2_SRC) | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/kernel.elf: $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS) $(KERNEL_LD_SCRIPT)
	$(CROSS_LD) -T $(KERNEL_LD_SCRIPT) -o $@ $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(CROSS_OBJCOPY) -O binary $< $@

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c | $(BUILD_DIR)/kernel
	$(CROSS_GCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.asm | $(BUILD_DIR)/kernel
	$(ASM) -f elf64 $< -o $@

$(HELLO_PROGRAM_ELF): $(PROGRAM_COMMON_C_OBJS) $(PROGRAM_COMMON_ASM_OBJS) $(HELLO_PROGRAM_OBJ) $(PROGRAM_LD_SCRIPT)
	$(CROSS_LD) -T $(PROGRAM_LD_SCRIPT) -o $@ $(PROGRAM_COMMON_ASM_OBJS) $(PROGRAM_COMMON_C_OBJS) $(HELLO_PROGRAM_OBJ)

$(ARGS_PROGRAM_ELF): $(PROGRAM_COMMON_C_OBJS) $(PROGRAM_COMMON_ASM_OBJS) $(ARGS_PROGRAM_OBJ) $(PROGRAM_LD_SCRIPT)
	$(CROSS_LD) -T $(PROGRAM_LD_SCRIPT) -o $@ $(PROGRAM_COMMON_ASM_OBJS) $(PROGRAM_COMMON_C_OBJS) $(ARGS_PROGRAM_OBJ)

$(BUILD_DIR)/programs/%.payload: $(BUILD_DIR)/programs/%.elf
	$(CROSS_OBJCOPY) -O binary $< $@

$(BUILD_DIR)/programs/%.BIN: $(BUILD_DIR)/programs/%.payload $(BUILD_DIR)/programs/%.elf $(PROGRAM_PACK_TOOL) Makefile
	bss_size=`$(CROSS_SIZE) -A -d $(BUILD_DIR)/programs/$*.elf | awk '$$1==".bss" {sum+=$$2} END {print sum+0}'`; \
	$(PROGRAM_PACK_TOOL) pack $(BUILD_DIR)/programs/$*.payload $@ 0 $$bss_size

$(BUILD_DIR)/programs/common/%.o: $(SRC_DIR)/programs/common/%.c | $(BUILD_DIR)/programs/common
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/programs/common/%.o: $(SRC_DIR)/programs/common/%.asm | $(BUILD_DIR)/programs/common
	$(ASM) -f elf64 $< -o $@

$(BUILD_DIR)/programs/hello/%.o: $(SRC_DIR)/programs/hello/%.c | $(BUILD_DIR)/programs/hello
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/programs/args/%.o: $(SRC_DIR)/programs/args/%.c | $(BUILD_DIR)/programs/args
	$(CROSS_GCC) $(PROGRAM_CFLAGS) -c $< -o $@

$(IMG_TOOL): tools/imgbuild/imgbuild.c | $(BUILD_DIR)/tools
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tools/imgbuild/imgbuild.c -o $@

$(FAT_TOOL): tools/fat/fat.c | $(BUILD_DIR)/tools
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror tools/fat/fat.c -o $@

$(PATH_TEST_TOOL): tests/path_test.c $(SRC_DIR)/kernel/path.c $(SRC_DIR)/kernel/kstring.c | $(BUILD_DIR)/tools
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I$(SRC_DIR)/common -I$(SRC_DIR)/kernel tests/path_test.c $(SRC_DIR)/kernel/path.c $(SRC_DIR)/kernel/kstring.c -o $@

$(PROGRAM_PACK_TOOL): tools/programpack/programpack.c $(SRC_DIR)/common/program_format.h | $(BUILD_DIR)/tools
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I$(SRC_DIR)/common tools/programpack/programpack.c -o $@

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/kernel:
	mkdir -p $@

$(BUILD_DIR)/tools:
	mkdir -p $@

$(BUILD_DIR)/programs:
	mkdir -p $@

$(BUILD_DIR)/programs/common:
	mkdir -p $@

$(BUILD_DIR)/programs/hello:
	mkdir -p $@

$(BUILD_DIR)/programs/args:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
