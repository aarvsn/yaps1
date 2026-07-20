/// @file bus.cpp
/// @brief SystemBus implementation — routes CPU memory accesses.

#include "common/bus.h"
#include "common/logger.h"
#include "gpu/gpu.h"
#include "spu/spu.h"
#include "cdrom/cdrom.h"
#include "dma/dma.h"
#include "timers/timers.h"
#include "controllers/controller.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace yaps1 {

// ===========================================================================
//  I/O address map (offsets from 0x1F801000)
// ===========================================================================

// Memory Control registers
inline constexpr u32 IO_MEMCTRL_BASE  = 0x0000;
// DMA registers
inline constexpr u32 IO_DMA_BASE     = 0x1000;  // actually 0x1F801080
// Timers
inline constexpr u32 IO_TIMER_BASE   = 0x1100;  // actually 0x1F801100
// GPU
inline constexpr u32 IO_GPU_BASE     = 0x1810;  // GP0 0x1F801810, GP1 0x1F801814
// SPU
inline constexpr u32 IO_SPU_BASE     = 0x1C00;
// CD-ROM
inline constexpr u32 IO_CDROM_BASE   = 0x1800;  // 0x1F801800
// Controller / Memory Card
inline constexpr u32 IO_JOY_BASE     = 0x1040;  // 0x1F801040
// Interrupt controller
inline constexpr u32 IO_IRQ_BASE     = 0x1070;  // 0x1F801070
// SIO (serial)
inline constexpr u32 IO_SIO_BASE     = 0x1050;

// ===========================================================================
//  Constructor / Reset
// ===========================================================================

Bus::Bus() { reset(); }

void Bus::reset() {
    ram_.fill(0);
    scratchpad_.fill(0);
    // NOTE: bios_ is NOT cleared — it was loaded from file.
    io_mirror_.fill(0);
    exp1_.fill(0);
    std::memset(mem_ctrl_, 0, sizeof(mem_ctrl_));
}

// ===========================================================================
//  BIOS loading
// ===========================================================================

Result<bool> Bus::load_bios(const Path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return std::unexpected(Error{"Cannot open BIOS file: " + path.string()});
    }

    auto size_raw = f.tellg();
    f.seekg(0, std::ios::beg);
    auto size = static_cast<i64>(size_raw);

    if (size != BIOS_SIZE && size != 512 * 1024) {
        return std::unexpected(Error{
            "BIOS file has wrong size: " + std::to_string(size) +
            " bytes (expected " + std::to_string(BIOS_SIZE) + ")"});
    }

    f.read(reinterpret_cast<char*>(bios_.data()), size);
    if (!f) {
        return std::unexpected(Error{"Failed to read BIOS file: " + path.string()});
    }

    LOG_INFO("BIOS loaded: {} ({} bytes)", path.filename().string(), size);
    return true;
}

// ===========================================================================
//  Physical address decoding
// ===========================================================================

u8 Bus::read8(u32 addr) {
    // The CPU already translates KSEG0/KSEG1 to physical.  Some code paths
    // may pass untranslated addresses (e.g. DMA), so handle that here too.
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        return ram_[phys];
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        return scratchpad_[phys - SCRATCHPAD_BASE];
    }
    if (phys >= BIOS_BASE && phys < BIOS_BASE + BIOS_SIZE) {
        return bios_[phys - BIOS_BASE];
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        return io_read8(phys);
    }
    if (phys >= EXP2_BASE && phys < EXP2_BASE + 0x2000) {
        return 0xFF;  // Expansion 2 — open bus
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        return exp1_[phys - 0x1F000000u];
    }

    // Open bus — return 0xFF (or last value on the bus).
    return 0xFF;
}

u16 Bus::read16(u32 addr) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        u16 val;
        std::memcpy(&val, &ram_[phys], 2);
        return val;
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        u16 val;
        std::memcpy(&val, &scratchpad_[phys - SCRATCHPAD_BASE], 2);
        return val;
    }
    if (phys >= BIOS_BASE && phys < BIOS_BASE + BIOS_SIZE) {
        u16 val;
        std::memcpy(&val, &bios_[phys - BIOS_BASE], 2);
        return val;
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        return io_read16(phys);
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        u16 val;
        std::memcpy(&val, &exp1_[phys - 0x1F000000u], 2);
        return val;
    }
    return 0xFFFF;
}

u32 Bus::read32(u32 addr) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        u32 val;
        std::memcpy(&val, &ram_[phys], 4);
        return val;
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        u32 val;
        std::memcpy(&val, &scratchpad_[phys - SCRATCHPAD_BASE], 4);
        return val;
    }
    if (phys >= BIOS_BASE && phys < BIOS_BASE + BIOS_SIZE) {
        u32 val;
        std::memcpy(&val, &bios_[phys - BIOS_BASE], 4);
        return val;
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        return io_read32(phys);
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        u32 val;
        std::memcpy(&val, &exp1_[phys - 0x1F000000u], 4);
        return val;
    }
    return 0xFFFFFFFF;
}

void Bus::write8(u32 addr, u8 val) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        ram_[phys] = val;
        return;
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        scratchpad_[phys - SCRATCHPAD_BASE] = val;
        return;
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        io_write8(phys, val);
        return;
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        exp1_[phys - 0x1F000000u] = val;
        return;
    }
}

void Bus::write16(u32 addr, u16 val) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        std::memcpy(&ram_[phys], &val, 2);
        return;
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        std::memcpy(&scratchpad_[phys - SCRATCHPAD_BASE], &val, 2);
        return;
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        io_write16(phys, val);
        return;
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        std::memcpy(&exp1_[phys - 0x1F000000u], &val, 2);
        return;
    }
}

void Bus::write32(u32 addr, u32 val) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE) {
        std::memcpy(&ram_[phys], &val, 4);
        return;
    }
    if (phys >= SCRATCHPAD_BASE && phys < SCRATCHPAD_BASE + SCRATCHPAD_SIZE) {
        std::memcpy(&scratchpad_[phys - SCRATCHPAD_BASE], &val, 4);
        return;
    }
    if (phys >= IO_BASE && phys < IO_BASE + IO_PORT_SIZE) {
        io_write32(phys, val);
        return;
    }
    if (phys >= 0x1F000000u && phys < 0x1F800000u) {
        std::memcpy(&exp1_[phys - 0x1F000000u], &val, 4);
        return;
    }
}

void Bus::read_block(u32 addr, ByteSpan& dst) {
    // Fast path for RAM.
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE && phys + dst.size() <= RAM_SIZE) {
        std::memcpy(const_cast<byte*>(dst.data()), &ram_[phys], dst.size());
        return;
    }
    // Fallback: byte-by-byte — need mutable copy.
    // Since ByteSpan is const, we can't write through it directly.
    // The caller should provide a mutable span. For now, use a const_cast.
}

void Bus::write_block(u32 addr, ByteSpan src) {
    u32 phys = addr;
    if (addr >= 0x80000000u) phys = addr & 0x1FFFFFFFu;

    if (phys < RAM_SIZE && phys + src.size() <= RAM_SIZE) {
        std::memcpy(&ram_[phys], src.data(), src.size());
        return;
    }
    for (auto b : src) {
        write8(addr, b);
        addr++;
    }
}

// ===========================================================================
//  I/O dispatch
// ===========================================================================

u8 Bus::io_read8(u32 addr) {
    u32 offset = addr - IO_BASE;

    // Joy pad data / memcard
    if (offset >= 0x0040 && offset < 0x0048) {
        if (controller_) return controller_->read8(offset - 0x0040);
    }

    // SPU
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) return spu_->read8(offset);
    }

    LOG_DEBUG("IO read8  {:04X} = FF", offset);
    return 0xFF;
}

u16 Bus::io_read16(u32 addr) {
    u32 offset = addr - IO_BASE;

    // Timers
    if (offset >= 0x1000 && offset < 0x1100) {
        if (timers_) return timers_->read16(offset - 0x1000);
    }

    // SPU
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) return spu_->read16(offset);
    }

    // Joy stat
    if (offset >= 0x0040 && offset < 0x0048) {
        if (controller_) return controller_->read16(offset - 0x0040);
    }

    // Interrupt status / mask
    if (offset == 0x0070) {
        // I_STAT — return current interrupt status
        u16 stat = 0;
        if (gpu_ && gpu_->irq_pending()) stat |= (1u << 0);
        if (cdrom_ && cdrom_->irq_pending()) stat |= (1u << 2);
        if (dma_ && dma_->irq_active()) stat |= (1u << 3);
        if (timers_) stat |= timers_->irq_bits() << 4;
        return stat;
    }
    if (offset == 0x0074) {
        // I_MASK
        return 0x7F;  // All IRQs enabled
    }

    LOG_DEBUG("IO read16 {:04X} = FFFF", offset);
    return 0xFFFF;
}

u32 Bus::io_read32(u32 addr) {
    u32 offset = addr - IO_BASE;

    // ---- Memory Control (0x0000-0x003F) ----
    if (offset < 0x0010) {
        int idx = offset / 4;
        if (idx < 4) return mem_ctrl_[idx];
        return 0;
    }

    // ---- DMA (0x0080-0x00FF) ----
    if (offset >= 0x0080 && offset < 0x0100) {
        if (dma_) return dma_->read32(offset - 0x0080);
        return 0;
    }

    // ---- Timers (0x1000-0x1100) ----
    if (offset >= 0x1000 && offset < 0x1100) {
        if (timers_) return timers_->read32(offset - 0x1000);
        return 0;
    }

    // ---- GPU (0x1810, 0x1814) ----
    if (offset == 0x0810) {
        if (gpu_) return gpu_->read_gp0();
        return 0;
    }
    if (offset == 0x0814) {
        if (gpu_) return gpu_->read_gp1();
        return 0;
    }

    // ---- CD-ROM (0x1800) ----
    if (offset >= 0x0800 && offset < 0x0840) {
        if (cdrom_) return cdrom_->read32(offset - 0x0800);
        return 0;
    }

    // ---- SPU (0x1C00) ----
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) return spu_->read32(offset);
        return 0;
    }

    // ---- Joy (0x1040) ----
    if (offset >= 0x0040 && offset < 0x0060) {
        if (controller_) return controller_->read32(offset - 0x0040);
        return 0;
    }

    LOG_DEBUG("IO read32 {:04X} = FFFFFFFF", offset);
    return 0xFFFFFFFF;
}

void Bus::io_write8(u32 addr, u8 val) {
    u32 offset = addr - IO_BASE;

    // SPU
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) spu_->write8(offset, val);
        return;
    }

    // Joy pad
    if (offset >= 0x0040 && offset < 0x0048) {
        if (controller_) controller_->write8(offset - 0x0040, val);
        return;
    }
}

void Bus::io_write16(u32 addr, u16 val) {
    u32 offset = addr - IO_BASE;

    // Timers
    if (offset >= 0x1000 && offset < 0x1100) {
        if (timers_) timers_->write16(offset - 0x1000, val);
        return;
    }

    // SPU
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) spu_->write16(offset, val);
        return;
    }

    // Joy
    if (offset >= 0x0040 && offset < 0x0060) {
        if (controller_) controller_->write16(offset - 0x0040, val);
        return;
    }

    // Interrupt mask / ack
    if (offset == 0x0070) {
        // I_STAT — writing 1 bits ACKs (clears) those IRQs.
        if (dma_) dma_->ack_irq(val);
        return;
    }
    if (offset == 0x0074) {
        // I_MASK — set IRQ mask.
        if (dma_) dma_->set_irq_mask(val);
        return;
    }
}

void Bus::io_write32(u32 addr, u32 val) {
    u32 offset = addr - IO_BASE;

    // ---- Memory Control ----
    if (offset < 0x0010) {
        int idx = offset / 4;
        if (idx < 4) mem_ctrl_[idx] = val;
        // BIOS writes these during POST. We just store them.
        return;
    }

    // ---- DMA (0x0080) ----
    if (offset >= 0x0080 && offset < 0x0100) {
        if (dma_) dma_->write32(offset - 0x0080, val);
        return;
    }

    // ---- Timers (0x1000) ----
    if (offset >= 0x1000 && offset < 0x1100) {
        if (timers_) timers_->write32(offset - 0x1000, val);
        return;
    }

    // ---- GPU GP0 (0x1810) ----
    if (offset == 0x0810) {
        if (gpu_) gpu_->write_gp0(val);
        return;
    }
    // ---- GPU GP1 (0x1814) ----
    if (offset == 0x0814) {
        if (gpu_) gpu_->write_gp1(val);
        return;
    }

    // ---- CD-ROM ----
    if (offset >= 0x0800 && offset < 0x0840) {
        if (cdrom_) cdrom_->write32(offset - 0x0800, val);
        return;
    }

    // ---- SPU ----
    if (offset >= 0x0C00 && offset < 0x0E00) {
        if (spu_) spu_->write32(offset, val);
        return;
    }

    // ---- Joy ----
    if (offset >= 0x0040 && offset < 0x0060) {
        if (controller_) controller_->write32(offset - 0x0040, val);
        return;
    }

    // ---- Interrupt acknowledge / mask ----
    if (offset == 0x0070) {
        if (dma_) dma_->ack_irq(val);
        return;
    }
    if (offset == 0x0074) {
        if (dma_) dma_->set_irq_mask(val);
        return;
    }
}

} // namespace yaps1