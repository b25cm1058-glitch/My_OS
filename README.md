# My_OS 🖥️

> A functional x86-64 OS kernel written in C 
---

## What is this?

A bare-metal operating system kernel that boots on real x86-64 hardware (and QEMU).  
No OS underneath. No libc. No runtime. Just C, inline assembly, and hardware.


---

## Features

### 🧠 Memory Management
- **Physical Memory Manager (PMM)** — bitmap-based page allocator (tracks RAM in 4KB frames)
- **Virtual Memory Manager (VMM)** — full 4-level paging (PML4 → PDPT → PD → PT)
- Parses Limine's memory map at boot to find all usable RAM
- Activates custom page tables by writing directly to `CR3`
- HHDM offset to safely bridge physical ↔ virtual addresses

### ⚡ Interrupt Handling
- **Interrupt Descriptor Table (IDT)** — 256 entries, fully initialized
- Division-by-zero and Page Fault handlers
- Page Fault handler reads `CR2` register for the exact faulting address
- **BSOD-style kernel panic screen** displayed on crash

### 🖥️ Graphics (Direct Framebuffer)
- Pixel-level rendering via Limine's Linear Framebuffer — no GPU driver needed
- 8×8 bitmap font renderer with full **RGB color control**
- Safe pixel function with bounds checking and clipping (no crash on out-of-bounds draw)
- Shape drawing:
  - Filled circle & hollow circle (Midpoint Circle Algorithm)
  - Filled rectangle
  - Triangle
  - Hexagon (sin60° approximated as 866/1000 — no FPU)
  - ❤️ Heart via parametric equation: `(x²+y²−r²)³ − x²y³r ≤ 0`
- Bresenham's Line Algorithm — integer only, zero floating point
- Screen scrolling

### ⌨️ Shell & Terminal
- Linux-style terminal with `root@myos:~$` prompt
- **PS/2 keyboard driver** — reads directly from hardware port `0x60`
- Shift key support (uppercase + symbols), blinking cursor animation
- **Command history** — navigate with ↑/↓ arrows, just like bash
- `$variable` expansion works inside all commands

### 📦 Variable System
- In-memory key-value store (up to 32 variables)
- `set <name> <value>` — store a variable
- `get <name>` — retrieve a variable
- `unset <name>` — delete a variable
- `vars` — list all variables
- `vartest` — built-in self-test suite with PASS/FAIL output

### 🔧 Built-in Commands

| Command | Description |
|---|---|
| `help` | List all commands |
| `about` | OS info |
| `clear` | Clear the screen |
| `echo <text>` | Print text (supports `$var` expansion) |
| `calc <a> <op> <b>` | Integer arithmetic (fixed-point, no FPU) |
| `time` | Real-time clock via RTC ports (0x70/0x71) |
| `color <hex>` | Change terminal text color |
| `sysinfo` | CPU vendor (via CPUID) and memory info |
| `shutdown` | Halt the system |
| `ask <question>` | Interactive y/n prompt |
| `rect`, `circle`, `hcircle` | Draw shapes |
| `tri`, `hex`, `hexagon`, `heart` | More shapes |
| `set`, `get`, `unset`, `vars`, `vartest` | Variable system |

---

## Project Structure

```
My_OS/
├── src/
│   ├── kernel.c      # Main kernel — all subsystems (1300+ lines)
│   └── font.h        # 8×8 bitmap font data
├── iso_root/         # Files packed into the bootable ISO
├── limine/           # Limine bootloader
├── limine.cfg        # Bootloader config
├── linker.ld         # Custom linker script (memory layout)
├── Makefile          # Build system
├── kernel.elf        # Compiled kernel (generated)
└── image.iso         # Bootable ISO (generated)
```

---

## How to Build & Run

### Requirements
- `gcc` cross-compiler targeting `x86_64-elf`
- `GNU Make`
- `xorriso` (for ISO creation)
- `QEMU` (for testing)

### Build
```bash
make
```

### Run in QEMU
```bash
qemu-system-x86_64 -cdrom image.iso
```

---

## Technical Highlights

- **No standard library** — `memset`, `strlen`, `strcmp`, `itoa` all written from scratch
- **Fixed-point arithmetic** — avoids floating-point operations entirely (no FPU dependency)
- **Inline x86-64 assembly** — used for `CR2`, `CR3`, `CPUID`, port I/O (`inb`/`outb`), and IDT loading (`lidt`)
- **Single translation unit** — entire kernel lives in one `kernel.c` file
- **Refactored** from 1800 → 1300 lines by extracting repeated logic into reusable functions

---

## Note on AI Usage

I used AI as a learning tool throughout this project — these concepts (paging, IDT, port I/O, bare-metal graphics) were way above intro level.

---

## Author

**Tushar Raghuvanshi**  
GitHub: [tushar-raghuvanshi](https://github.com/tushar-raghuvanshi/My_OS)
