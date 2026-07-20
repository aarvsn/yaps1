# YAPS1 — Yet Another PlayStation 1 Emulator

A terminal-based PlayStation 1 emulator written in C++20. No GUI — just a command line and raw speed.

## Features

- **MIPS R3000A CPU** interpreter with branch/load delay slots, COP0 exceptions
- **GPU** software renderer (1024×512 VRAM, triangles, textures, dithering)
- **SPU** with 24 ADPCM voices, ADSR envelopes, 512KB SPU RAM
- **CD-ROM** controller with CUE/BIN/ISO parsing, sector timing, IRQ2
- **7-channel DMA** (MDEC, GPU, CDROM, SPU, PIO, OTC) — block & linked-list modes
- **Memory card** save/load (card1.mcd / card2.mcd)
- **Save states** with optional zlib compression
- **Timer subsystem** (root counters 0–2, sysclk, dotclk, hblank)
- **SDL3 input** — keyboard & gamepad hot-plug
- **TOML config** auto-generated on first run
- **Optional Vulkan** renderer (compile-time toggle)
- **Cross-platform** — Windows (MSVC/MinGW), Linux, macOS

## Quick Start

```bash
# Clone
git clone https://github.com/aarvsn/yaps1.git
cd yaps1

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/YAPS1 game.bin
./build/YAPS1 game.cue
```

Or use the install script:
```bash
chmod +x install.sh
./install.sh          # installs to ~/.local
./install.sh /usr/local  # system-wide
```

## BIOS

YAPS1 requires a PS1 BIOS ROM. Place `SCPH1001.BIN` (512 KB) next to your game disc image, or set the path in `config.toml`:

```toml
[system]
bios_path = "/path/to/SCPH1001.BIN"
```

## Building

### Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| CMake ≥ 3.22 | Build system | Yes |
| C++23 compiler | GCC 13+, Clang 16+, MSVC 2022+ | Yes |
| [SDL3](https://github.com/libsdl-org/SDL) | Input handling | Yes (fetched) |
| [fmt](https://github.com/fmtlib/fmt) | Logging | Yes (fetched) |
| Vulkan SDK | Vulkan renderer | No (optional) |
| zlib | Compressed save states | No (optional) |

SDL3 and fmt are fetched automatically via CMake's FetchContent. Vulkan and zlib are detected at configure time if installed.

### Build Options

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Project Structure

```
src/
  common/     types, memory map, bus, logger, config
  cpu/        MIPS R3000A interpreter, COP0, exceptions
  gpu/        GP0/GP1 commands, VRAM, software rasterizer
  spu/        24 ADPCM voices, ADSR, SPU RAM
  cdrom/      CUE/BIN parser, command/response FIFOs, sector reads
  dma/        7 DMA channels, block & linked-list transfer modes
  timers/     Root counters 0–2, system clock sources
  controllers/ SDL3 input, keyboard mapping, gamepad hot-plug
  memcards/   Memory card file I/O (128 KB cards)
  savestates/ State serialization (plain & zlib-compressed)
  frontend/   System class, main loop, argument parsing
```

## Usage

```
YAPS1 <game.bin|game.cue|game.iso> [options]
```

Supported disc formats: `.bin` (raw), `.cue/.bin` (CUE sheet), `.img`, `.iso` (2048-byte mode 2).

## License

MIT, see [LICENSE](LICENSE).
