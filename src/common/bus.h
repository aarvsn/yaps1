#pragma once

/// @file bus.h
/// @brief Central system bus — routes all memory accesses to the correct
///        subsystem (RAM, scratchpad, GPU, SPU, CDROM, DMA, timers, etc.).

#include "common/types.h"
#include "common/memory_map.h"
#include <array>
#include <memory>

namespace yaps1 {

class Cpu;
class Gpu;
class Spu;
class Cdrom;
class Dma;
class Timers;
class Pad;
class MemoryCard;

/// The Bus implements the PS1 memory map.  Every CPU read/write goes through
/// this class, which dispatches to the appropriate hardware subsystem.
class Bus final : public BusInterface {
public:
    Bus();
    ~Bus() override = default;

    void reset();

    // ---- Subsystem pointers (set by System during init) ----
    void set_cpu(Cpu* c)         { cpu_ = c; }
    void set_gpu(Gpu* g)         { gpu_ = g; }
    void set_spu(Spu* s)         { spu_ = s; }
    void set_cdrom(Cdrom* cd)    { cdrom_ = cd; }
    void set_dma(Dma* d)         { dma_ = d; }
    void set_timers(Timers* t)   { timers_ = t; }
    void set_controller(Pad* c) { controller_ = c; }
    void set_memcard(MemoryCard* m) { memcard_ = m; }

    // ---- Access to raw RAM (for DMA, BIOS loading, save-states) ----
    [[nodiscard]] u8* ram()             { return ram_.data(); }
    [[nodiscard]] u8* scratchpad()      { return scratchpad_.data(); }
    [[nodiscard]] u8* bios_data()       { return bios_.data(); }

    /// Load a BIOS image into the BIOS ROM region.
    Result<bool> load_bios(const Path& path);

    // ---- BusInterface implementation ----
    u8  read8(u32 addr) override;
    u16 read16(u32 addr) override;
    u32 read32(u32 addr) override;
    void write8(u32 addr, u8 val) override;
    void write16(u32 addr, u16 val) override;
    void write32(u32 addr, u32 val) override;
    void read_block(u32 addr, ByteSpan& dst) override;
    void write_block(u32 addr, ByteSpan src) override;

private:
    // ---- Internal dispatch helpers ----
    u32  io_read32(u32 addr);
    u16  io_read16(u32 addr);
    u8   io_read8(u32 addr);
    void io_write32(u32 addr, u32 val);
    void io_write16(u32 addr, u16 val);
    void io_write8(u32 addr, u8 val);

    // ---- Subsystem pointers ----
    Cpu*        cpu_        = nullptr;
    Gpu*        gpu_        = nullptr;
    Spu*        spu_        = nullptr;
    Cdrom*      cdrom_      = nullptr;
    Dma*        dma_        = nullptr;
    Timers*     timers_     = nullptr;
    Pad* controller_ = nullptr;
    MemoryCard* memcard_    = nullptr;

    // ---- Memory regions ----
    std::array<u8, RAM_SIZE>        ram_{};
    std::array<u8, SCRATCHPAD_SIZE> scratchpad_{};
    std::array<u8, BIOS_SIZE>       bios_{};

    // ---- I/O region scratch ----
    /// I/O register mirror (8 KB, covers 0x1F801000 - 0x1F802FFF)
    std::array<u8, IO_PORT_SIZE>    io_mirror_{};

    // ---- Expansion region 1 (0x1F000000-0x1F800000) ----
    /// Mostly unused; some games write here.
    std::array<u8, 8 * MIB>        exp1_{};

    // ---- POST / memory control registers ----
    /// These are at I/O offsets 0x1000-0x10FF (memory control).
    /// We store them for games that read them back.
    u32 mem_ctrl_[4]{};
};

} // namespace yaps1