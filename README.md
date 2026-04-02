# FunnyOS

FunnyOS is a small x86 BIOS-booted operating system project based on Nanobyte's tutorial series, migrated toward a Watcom-free toolchain and a QEMU-first workflow.

The current runtime path is:
- `stage1`: 16-bit FAT12 boot sector in NASM
- `stage2`: NASM-only real-mode loader that reads FAT12, loads `KERNEL.BIN`, prepares boot info, and switches to 32-bit protected mode
- `kernel`: freestanding 32-bit C/ASM kernel built with a GNU cross-toolchain

## Current features

- Boots from a generated FAT12 floppy image
- Loads `STAGE2.BIN` and `KERNEL.BIN` from the root directory
- Hands a `BootInfo` structure from the loader to the kernel
- Prints the preserved demo output over VGA text mode and COM1 serial during loader bring-up
- Preserves the current demo behavior:
  - prints a boot banner
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

The default build no longer depends on:
- Open Watcom
- Bochs
- `mkfs.fat`
- `mcopy`

## Layout

```text
.
├── src/boot/stage1/      # BIOS boot sector
├── src/boot/stage2/      # FAT12 loader + protected-mode transition
├── src/common/           # Minimal shared boot/kernel structures
├── src/kernel/           # Minimal protected-mode handoff kernel
├── tools/imgbuild/       # Repo-owned FAT12 image builder
├── tools/fat/            # Host FAT12 inspection tool
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
- `build/stage1.bin`
- `build/stage2.bin`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/main_floppy.img`

If the cross-toolchain or assembler is missing, `make` fails fast with a tool-specific error.

## Run

```sh
make run
```

or:

```sh
./run.sh
```

The default run path boots the floppy image in QEMU and forwards COM1 serial output to the terminal.

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
- host-side FAT12 image inspection with the repo tool
- normal QEMU boot output
- missing-kernel error handling
- missing-demo-file error handling

If QEMU is not installed, the test script still runs the host-side FAT12 validation and reports that QEMU checks were skipped.

## Notes

- The kernel is linked for `0x00100000` and entered in 32-bit protected mode without paging.
- The initial `BootInfo` contract leaves memory-map fields at zero; later kernel work can extend that handoff.
- The current demo text is emitted by `stage2` immediately before the protected-mode jump while the kernel-side console path is being hardened.
- Serial output exists mainly to make QEMU smoke tests deterministic.
- Most of the logic now lives in `stage2`; the kernel is intentionally tiny so the codebase stays easy to re-learn.
