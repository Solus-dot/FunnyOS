# FunnyOS

FunnyOS is a small x86 hobby operating system built as a learning project and shaped into a cleaner, standard-tooling codebase over time. It started from the ideas in Nanobyte's tutorial series, but the current project is its own BIOS-booted, hard-disk-based OS with a protected-mode kernel, a read-only FAT16 runtime filesystem, and a tiny interactive shell.

The focus right now is not "do everything," but "do the fundamentals cleanly":
- standard tools only: `nasm`, GNU cross-compiler tools, `make`, and `qemu`
- a real hard-disk boot flow instead of floppy-only assumptions
- a small kernel that reads files at runtime instead of relying on loader demos
- a codebase that is easier to extend into something more OS-like

## What It Does

Today, FunnyOS can:
- boot from a generated hard-disk image using `MBR -> stage1 -> stage2 -> kernel`
- load `STAGE2.BIN` and `KERNEL.BIN` from a FAT16 partition
- switch into 32-bit protected mode
- access the boot disk at runtime using ATA PIO
- mount a read-only FAT16 filesystem inside the kernel
- provide a small shell with built-ins and external flat-binary programs

Current shell commands:
- `help`
- `ls [path]`
- `cd <path>`
- `pwd`
- `cat <path>`
- `clear`

Current bundled external programs:
- `HELLO`
- `ARGS`

It also supports:
- absolute and relative paths
- `.` and `..`
- case-insensitive FAT 8.3 lookup
- multi-cluster files and directories
- flat-binary program loading from the FAT16 disk image
- a tiny syscall-style program ABI for output, input, and exit

## Project Status

FunnyOS is in the "solid core" stage.

That means:
- the boot path works
- the runtime filesystem works
- the shell is usable
- external programs can be loaded and run
- the internals have started to separate into cleaner layers

That also means it is still intentionally limited:
- BIOS only, not UEFI
- FAT16 only
- read-only filesystem
- no multitasking
- no paging
- no AHCI, SATA, or NVMe support

## Architecture

The current runtime stack is split like this:
- `mbr`: tiny BIOS MBR that finds the active partition and loads the partition boot sector
- `stage1`: FAT16 volume boot sector that loads `STAGE2.BIN`
- `stage2`: real-mode loader that reads FAT16, loads the kernel, prepares `BootInfo`, and enters protected mode
- `block`: boot-disk sector I/O layer in the kernel
- `fs`: filesystem-facing kernel API used by the shell
- `fat16`: read-only FAT16 driver behind the `fs` API
- `path`: canonical path normalization for shell and filesystem access
- `program`: flat-binary loader plus tiny kernel-owned program ABI
- `shell`: user-facing command loop and dispatcher

This keeps the shell from depending directly on FAT16 internals and makes future milestones easier to implement.

## Program Model

FunnyOS can now run tiny external programs stored as ordinary FAT16 files.

The current model is intentionally small:
- programs are raw flat binaries, not ELF executables
- they are loaded at a fixed address into the same address space as the kernel
- there is no memory protection, multitasking, or process isolation yet
- programs return control to the shell through a tiny kernel ABI

Right now the ABI exposes only a few essentials:
- write text to the console/serial output
- read a line of input
- exit back to the shell

That is enough to move beyond a built-in-only shell without pretending this is already a full DOS-like environment.

## Quick Start

### Requirements

You need:
- `nasm`
- `make`
- `qemu-system-i386`
- a GNU cross toolchain:
  - `i686-elf-gcc`, `i686-elf-ld`, `i686-elf-objcopy`
  - or `i386-elf-gcc`, `i386-elf-ld`, `i386-elf-objcopy`
- a host C compiler for repo tools:
  - `gcc` or compatible `cc`

The default build does not depend on:
- Open Watcom
- Bochs
- `mkfs.fat`
- `mcopy`

### Build

```sh
make
```

This builds:
- `build/mbr.bin`
- `build/stage1.bin`
- `build/stage2.bin`
- `build/kernel.elf`
- `build/kernel.bin`
- `build/programs/HELLO.BIN`
- `build/programs/ARGS.BIN`
- `build/funnyos-disk.img`

### Run

```sh
make run
```

or:

```sh
./run.sh
```

This boots the generated hard-disk image in QEMU and forwards serial output to your terminal.

### Debug

```sh
make debug
```

or:

```sh
./debug.sh
```

This starts QEMU paused with a GDB stub exposed through `-s -S`.

### Test

```sh
make test
```

The test suite covers:
- host-side FAT16 image inspection
- host-side path normalization checks
- layering checks to ensure the shell uses `fs`, not `fat16`, directly
- booting to the shell prompt in QEMU
- shell command execution over serial
- external program loading and return-to-shell behavior
- multi-cluster file and directory reads
- missing-kernel error handling

If QEMU is unavailable, the host-side checks still run.

## Repository Layout

```text
.
├── src/boot/mbr/         # BIOS MBR
├── src/boot/stage1/      # FAT16 partition boot sector
├── src/boot/stage2/      # FAT16 loader + protected-mode transition
├── src/common/           # Shared boot/kernel structures
├── src/kernel/           # Kernel, storage, filesystem, shell, program loader
├── src/programs/         # Tiny flat-binary sample programs
├── tools/imgbuild/       # Hard-disk/FAT16 image builder
├── tools/fat/            # Host FAT16 inspection tool
├── tests/                # Smoke tests and host-side checks
├── run.sh
├── debug.sh
└── Makefile
```

The repo has been trimmed so the active path is the only path left. There is no legacy Watcom build flow hiding in the tree anymore.

## Disk Layout

The generated image is a fixed `64 MiB` raw disk with:
- one active MBR partition
- a FAT16 filesystem
- the partition starting at LBA `2048`

The kernel is linked at `0x00100000` and entered in 32-bit protected mode without paging.

## Limitations

FunnyOS is currently best treated as:
- a QEMU-first hobby OS
- a clean learning base for boot, disk, filesystem, and shell work
- a stepping stone toward a bigger DOS-like or general hobby OS

It should not be described as portable to arbitrary x86 hardware yet. The current runtime assumes legacy BIOS-style boot and IDE-compatible disk access.

## Why This Project Exists

The point of FunnyOS is to keep the project understandable while still being real enough to grow:
- the boot path is explicit
- the disk image is built inside the repo
- the kernel reads its own files at runtime
- the code is being refactored toward cleaner internal boundaries instead of piling features on tutorial code

If you want to work on an OS without jumping straight into a huge codebase, that is the niche this project is trying to fill.
