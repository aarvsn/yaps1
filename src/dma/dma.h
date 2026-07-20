#pragma once

/// @file dma.h
/// @brief PS1 DMA controller — 7 channels for GPU, SPU, CDROM, etc.

#include "common/types.h"
#include <array>
#include <functional>

namespace yaps1 {

class BusInterface;
class Gpu;
class Spu;
class Cdrom;

class Dma {
public:
    Dma();
    ~Dma() = default;

    void reset();

    void set_bus(BusInterface* bus)       { bus_ = bus; }
    void set_gpu(Gpu* gpu)               { gpu_ = gpu; }
    void set_spu(Spu* spu)               { spu_ = spu; }
    void set_cdrom(Cdrom* cdrom)         { cdrom_ = cdrom; }

    /// Read/write DMA registers (offset from 0x1F801080).
    void write32(u32 offset, u32 val);
    [[nodiscard]] u32 read32(u32 offset);

    /// Acknowledge IRQ bits.
    void ack_irq(u32 bits);
    /// Set the IRQ mask register.
    void set_irq_mask(u32 mask);

    /// Trigger a DMA transfer for the given channel.
    void trigger(u32 channel);

    /// Tick DMA timing.
    void tick(u32 cycles);

    /// Check if any DMA IRQ is pending and should be signaled to CPU.
    [[nodiscard]] bool irq_active() const;

private:
    // DMA channels
    static constexpr int NUM_CHANNELS = 7;

    enum Channel {
        MDEC_IN  = 0,  // Memory to MDEC
        MDEC_OUT = 1,  // MDEC to memory
        GPU      = 2,  // GPU
        CDROM    = 3,  // CD-ROM
        SPU      = 4,  // SPU
        PIO      = 5,  // PIO (parallel I/O)
        OTC      = 6,  // Reverse-clear OT (ordering table)
    };

    struct DmaChannel {
        u32 base_addr = 0;     // Transfer base address
        u32 block_ctrl = 0;    // Block control (BS:BC or transfer count)
        u32 channel_ctrl = 0;  // Channel control (direction, mode, etc.)
    };

    std::array<DmaChannel, NUM_CHANNELS> channels_{};

    // ---- DMA control register (0x1F8010F0) ----
    u32 control_ = 0;

    // ---- DMA interrupt register (0x1F8010F4) ----
    u32 irq_enable_ = 0;   // Which channels can trigger IRQ
    u32 irq_flags_ = 0;    // Which channels have completed
    u32 irq_flag_29_ = 0;  // Force IRQ bit
    bool irq_master_ = false;

    // ---- Interrupt callback ----
    // The System will poll irq_active() or we can call a callback.
    std::function<void(u32)> on_irq_;

    // ---- Internal ----
    void do_transfer(int ch);
    void transfer_block(int ch);
    void transfer_linked_list(int ch);

    BusInterface* bus_ = nullptr;
    Gpu*  gpu_   = nullptr;
    Spu*  spu_   = nullptr;
    Cdrom* cdrom_ = nullptr;
};

} // namespace yaps1