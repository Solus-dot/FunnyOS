# FunnyOS

FunnyOS is a small x86-64 hobby OS that is being migrated to a UEFI-only, FAT32-first architecture.

The active tree now targets:
- UEFI boot via `BOOTX64.EFI`
- a single staged FAT-style root under `build/esp`
- a 64-bit kernel loaded as `KERNEL.ELF`
- ELF sample programs
- a FAT32-backed runtime filesystem layer in the kernel

## Current Status

The repository now boots through the UEFI path under QEMU/OVMF, mounts the staged FAT32 runtime volume, and reaches the shell. The old BIOS, FAT16, and packed-program path has been removed from the mainline tree.

What currently works in-tree:
- `make image`
- `make run`
- `make test`
- UEFI boot through `BOOTX64.EFI`
- loading `KERNEL.ELF`
- FAT32-backed shell file operations
- running the bundled ELF sample programs

## Requirements

You need:
- `nasm`
- `make`
- `qemu-system-x86_64`
- `x86_64-elf-gcc`
- `x86_64-elf-ld`
- `x86_64-elf-objcopy`
- a host C compiler such as `gcc` or `cc`

The `Makefile` looks for common OVMF locations on Homebrew and Linux installs.

## Commands

Build the staged image tree:

```sh
make image
```

Run under QEMU + OVMF:

```sh
make run
```

Run the lightweight host-side test:

```sh
make test
```

The convenience wrappers still forward to the same targets:

```sh
./run.sh
./debug.sh
```

## Repository Layout

```text
.
├── src/boot/uefi/       # UEFI bootloader sources
├── src/common/          # Shared boot/kernel/program structures
├── src/kernel/          # Kernel, console, storage, FAT32 FS, shell, ELF loader
├── src/programs/        # Sample ELF programs
├── tests/               # Host-side checks
├── run.sh
├── debug.sh
└── Makefile
```

## Notes

- The old BIOS boot chain, FAT16 runtime driver, packed `.BIN` executable format, and legacy host image tools were intentionally removed from the mainline tree.
- The current runtime is still single-address-space and shell-first; user-mode isolation, paging work, and broader hardware support remain future milestones.
