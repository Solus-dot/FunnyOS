# FunnyOS

**FunnyOS** is a minimalist, custom-built operating system written in C and Assembly. Designed as an educational and experimental project, it provides a simple but functional environment to explore OS development fundamentals, including kernel development, bootloader integration, and low-level system programming.

## Features

- **Custom Kernel** written in C and Assembly  
- **Manual Bootloader Setup** using FAT12-formatted disk image  
- **Runs on Emulators** like Bochs (preferred) or QEMU  
- **Scripts** for automated building, running, and debugging  
- **Educational Structure** meant for learning and extending  

## Project Structure

```
.
├── .vscode/         # VSCode configurations
├── build/           # Build outputs
├── src/             # OS source code (C + Assembly)
├── tools/fat/       # FAT filesystem tools
├── bochs_config     # Configuration for Bochs emulator
├── debug.sh         # Script to run Bochs with debugger
├── run.sh           # Script to run the OS in Bochs
├── Makefile         # Build system using make
└── test.txt         # Test file (optional)
```

## Requirements / Dependencies

- **Open Watcom** (C compiler)
- **NASM** (for Assembly files)
- **Bochs** (emulator for x86 hardware)
- **Make** (build automation)

> Ensure all tools above are installed and available in your system `PATH`.

## Getting Started

### Clone and Build

```bash
git clone https://github.com/Solus-dot/FunnyOS.git
cd FunnyOS
make
```

### Run the OS

```bash
./run.sh
```

This will compile the OS and boot it in Bochs using the pre-defined `bochs_config`.

### Debugging

To start the OS with Bochs in debug mode:

```bash
./debug.sh
```

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for full details.
