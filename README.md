# FunnyOS

FunnyOS is a small x86 BIOS-booted operating system project based on Nanobyte's tutorial series, migrated onto a Watcom-free GNU toolchain with QEMU as the default runtime.

The current boot chain is:
- `mbr`: a tiny NASM MBR that finds the active partition and loads the partition boot sector
- `stage1`: a NASM FAT16 partition boot sector that loads `STAGE2.BIN`
- `stage2`: a NASM real-mode loader that reads FAT16, loads `KERNEL.BIN`, prepares `BootInfo`, and switches to 32-bit protected mode
- `kernel`: a minimal freestanding 32-bit C/ASM kernel built with a GNU cross-toolchain

## Current features

- Boots from a generated hard-disk image, not a floppy image
- Uses `MBR + one active FAT16 partition`
- Loads `STAGE2.BIN` and `KERNEL.BIN` from the FAT16 root directory
- Loads `MYDIR/TEST.TXT` from the FAT16 filesystem during stage 2
- Hands a minimal `BootInfo` structure from the loader to the kernel
- Preserves the current demo behavior:
  - prints the stage 2 boot banner
  - lists root directory entries
  - reads and displays `MYDIR/TEST.TXT`

## Toolchain

Required tools:
- `nasm`
- `make`
- `qemu-system-i386`
- GNU cross toolchain:
  - `i686-elf-gcc`, `i686-elf-ld`, `i686-elf-objcopy`
  - or `i386-elf-gcc`, `i386-elf-ld`, `i386-elf-objcopy`
- host C compiler for repo tools:
  - `gcc` or compatible `cc`

The default build does not depend on:
- Open Watcom
- Bochs
- `mkfs.fat`
- `mcopy`

## Layout

```text
.
├── src/boot/mbr/         # BIOS MBR
├── src/boot/stage1/      # FAT16 partition boot sector
├── src/boot/stage2/      # FAT16 loader + protected-mode transition
├── src/common/           # Minimal shared boot/kernel structures
├── src/kernel/           # Minimal protected-mode handoff kernel
├── tools/imgbuild/       # Repo-owned hard-disk/FAT16 image builder
├── tools/fat/            # Host FAT16 inspection tool
├── tests/                # Smoke tests
├── run.sh
├── debug.sh
└── Makefile
```

The repo was intentionally trimmed so the active code path is the only code path left in the tree.

## Build

```sh
make
```

This produces:
- `build/mbr.bin`
- `build/stage1.bin`
- `build/stage2.bin`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/funnyos-disk.img`

If the cross-toolchain or assembler is missing, `make` fails fast with a tool-specific error.

## Run

```sh
make run
```

or:

```sh
./run.sh
```

The default run path boots the hard-disk image in QEMU as an IDE disk and forwards COM1 serial output to the terminal.

## Debug

```sh
make debug
```

or:

```sh
./debug.sh
```

This starts QEMU paused with a GDB stub exposed via `-s -S`.

## Test

```sh
make test
```

The smoke test covers:
- host-side FAT16 image inspection with the repo tool
- normal QEMU boot output
- missing-kernel error handling
- missing-demo-file error handling

If QEMU is not installed, the test script still runs the host-side FAT16 validation and reports that QEMU checks were skipped.

## Notes

- The default disk image is a fixed `64 MiB` raw image with a single active FAT16 partition starting at LBA `2048`.
- The kernel is linked for `0x00100000` and entered in 32-bit protected mode without paging.
- `BootInfo` currently carries boot drive, partition, sector-size, and screen metadata only.
- The current demo text is still emitted by `stage2`; the kernel is intentionally minimal in this milestone.
- Serial output exists mainly to make QEMU smoke tests deterministic.
