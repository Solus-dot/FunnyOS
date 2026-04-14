# FunnyOS

FunnyOS is a small x86-64 hobby OS with a UEFI boot path, a FAT32 runtime filesystem, a simple shell, and ELF-loaded sample programs. The current tree has already dropped the old BIOS/FAT16/custom-binary path; the UEFI/FAT32/ELF stack is the mainline architecture.

## What It Does

Today, FunnyOS can:
- boot under QEMU + OVMF through `BOOTX64.EFI`
- load `KERNEL.ELF` from the same FAT32 disk image it later mounts at runtime
- initialize a 64-bit freestanding kernel
- access the boot disk at runtime through either ATA PIO or AHCI
- mount a writable FAT32 volume in the kernel
- provide a shell with basic file and directory commands
- load and run tiny external ELF programs

Current shell commands:
- `help`
- `ls [path]`
- `cd <path>`
- `pwd`
- `cat <path>`
- `clear`
- `mkdir <path>`
- `write <path> <text>`
- `append <path> <text>`
- `rm <path>`
- `mv <old> <new>`
- `memstat`
- `blockinfo`
- `lspci`

Bundled sample programs:
- `HELLO`
- `ARGS`

## Current Model

The current runtime model is intentionally small:
- single address space
- one foreground shell
- no multitasking
- no privilege separation
- no user-mode isolation yet

Programs are ELF files stored on the FAT32 volume. The kernel validates the ELF image, loads it into a fixed runtime region, and transfers control through a tiny FunnyOS program API for console output, line input, and exit.

## Build Requirements

You need:
- `nasm`
- `make`
- `qemu-system-x86_64`
- `x86_64-elf-gcc`
- `x86_64-elf-ld`
- `x86_64-elf-objcopy`
- `x86_64-w64-mingw32-gcc`
- a host C compiler such as `gcc` or `cc`
- either:
  - `hdiutil` on macOS, or
  - `mtools` (`mformat`, `mcopy`, `mmd`) on Linux

The `Makefile` searches common OVMF locations on Homebrew and Linux installs. If your firmware files are elsewhere, override `OVMF_CODE` and `OVMF_VARS` when invoking `make`.

## Build And Run

Build the disk image:

```sh
make image
```

Boot the OS under QEMU + OVMF with the legacy IDE model in a normal QEMU window:

```sh
make run
```

Boot the OS under QEMU + OVMF with an AHCI-backed SATA disk in a normal QEMU window:

```sh
make run-ahci
```

Explicit window-only aliases:

```sh
make run-window
make run-ahci-window
```

Start QEMU paused with a GDB stub:

```sh
make debug
```

Run the current host-side test target:

```sh
make test
```

Convenience wrappers:

```sh
./run.sh
./debug.sh
```

Headless serial-terminal variants:

```sh
make run-headless
make run-ahci-headless
```

`make run` and `make run-ahci` use the QEMU window path. The `*-headless` targets keep serial-terminal I/O.

## Boot And Runtime Layout

The current flow is:

1. QEMU boots OVMF.
2. OVMF loads `EFI/BOOT/BOOTX64.EFI` from the generated FAT32 image.
3. The UEFI loader reads `KERNEL.ELF`, gathers boot/device metadata, exits boot services, and jumps into the kernel.
4. The kernel initializes the console, PCI discovery, paging, the runtime block backend, the FAT32 filesystem, and the shell.
5. The shell can launch `HELLO.ELF` and `ARGS.ELF` from the root volume.

The generated image is staged under `build/esp` and then packed into `build/funnyos.img`.

## Repository Layout

```text
.
├── src/boot/uefi/       # UEFI bootloader
├── src/common/          # Shared boot/program/kernel structures
├── src/kernel/          # Kernel, ATA, FAT32, shell, ELF program loader
├── src/programs/        # Sample ELF programs
├── tests/               # Host-side tests
├── run.sh
├── debug.sh
└── Makefile
```

## Limitations

FunnyOS is still an early hobby OS. Important current limits:
- QEMU-first, not a broadly portable hardware target
- UEFI boot only
- runtime storage is still limited to ATA PIO or AHCI
- serial console only in the current mainline run flow
- no scheduler
- no paging-based process isolation
- no user-mode execution yet
- no networking, USB keyboard/input stack, NVMe, graphics UI, or SMP support

The FAT32 layer is usable, but the project is still in the “kernel core plus shell” stage, not a general-purpose OS.

## Status

What is working now:
- `make image`
- `make run`
- `make run-ahci`
- `make test`
- UEFI boot to the shell in QEMU
- FAT32-backed file and directory operations
- runtime FAT32 over both ATA PIO and AHCI in QEMU
- ELF sample program loading

What remains for the longer roadmap:
- real exception diagnostics
- allocator and paging work
- user-mode execution and syscall boundary
- broader hardware support
