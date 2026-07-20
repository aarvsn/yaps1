#pragma once

/// @file memory_map.h
/// @brief PS1 physical memory map and bus interface declarations.

#include "common/types.h"
#include <functional>

namespace yaps1 {

// ----------------------------------------------------------------- PS1 memory map (KSEG0 / physical)
// The R3000A has a 32-bit address space.  KSEG0 (0x8000_0000 .. 0x9FFF_FFFF)
// maps to physical 0x0000_0000 .. 0x1FFF_FFFF with the top bits cleared.
//
// Key regions (physical addresses):
//   0x0000_0000 - 0x001F_FFFF   Kernel / RAM (first 128 KB used by BIOS)
//   0x1F80_0000 - 0x1F80_03FF   Scratchpad (1 KB)
//   0x1F80_1000 - 0x1F80_1FFF   I/O ports
//   0x1F80_2000 - 0x1F80_2FFF   Expansion region 2
//   0x1FC0_0000 - 0x1FC7_FFFF   BIOS ROM (512 KB)

// Sizes
inline constexpr u32 KIB = 1024;
inline constexpr u32 MIB = 1024 * KIB;

inline constexpr u32 RAM_SIZE         = 2 * MIB;   // 2 MB main RAM
inline constexpr u32 SCRATCHPAD_SIZE  = 1 * KIB;    // 1 KB scratchpad
inline constexpr u32 BIOS_SIZE        = 512 * KIB;  // 512 KB BIOS ROM
inline constexpr u32 IO_PORT_SIZE     = 8 * KIB;    // 8 KB I/O

// Region base addresses (physical)
inline constexpr u32 RAM_BASE        = 0x00000000u;
inline constexpr u32 SCRATCHPAD_BASE = 0x1F800000u;
inline constexpr u32 IO_BASE         = 0x1F801000u;
inline constexpr u32 EXP2_BASE       = 0x1F802000u;
inline constexpr u32 BIOS_BASE       = 0x1FC00000u;

// ----------------------------------------------------------------- Bus access width tags
enum class Width : u8 { Byte = 1, Half = 2, Word = 4 };

// ----------------------------------------------------------------- BusInterface
/// Abstract bus interface that the CPU uses for all memory accesses.
/// The concrete SystemBus routes reads/writes to the correct hardware.
class BusInterface {
public:
    virtual ~BusInterface() = default;

    virtual u8  read8(u32 addr) = 0;
    virtual u16 read16(u32 addr) = 0;
    virtual u32 read32(u32 addr) = 0;

    virtual void write8(u32 addr, u8 val) = 0;
    virtual void write16(u32 addr, u16 val) = 0;
    virtual void write32(u32 addr, u32 val) = 0;

    /// Read a block of bytes (e.g. for DMA).
    virtual void read_block(u32 addr, ByteSpan& dst) = 0;

    /// Write a block of bytes (e.g. for DMA).
    virtual void write_block(u32 addr, ByteSpan src) = 0;
};

} // namespace yaps1