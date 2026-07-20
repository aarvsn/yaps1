/// @file dma.cpp
/// @brief DMA controller implementation.

#include "dma/dma.h"
#include "common/memory_map.h"
#include "gpu/gpu.h"
#include "spu/spu.h"
#include "cdrom/cdrom.h"
#include "common/logger.h"
#include <cstring>

namespace yaps1 {

Dma::Dma() { reset(); }

void Dma::reset() {
    for (auto& ch : channels_) {
        ch = DmaChannel{};
    }
    control_ = 0x07654321;  // After reset
    irq_enable_ = 0;
    irq_flags_ = 0;
    irq_flag_29_ = 0;
    irq_master_ = false;
}

// ===========================================================================
//  Register layout (offsets from 0x1F801080)
//  Each channel: 0x00=MADR, 0x04=BCR, 0x08=CHCR (8 bytes per channel)
//  0x70=DMA control (DPCR)
//  0x74=DMA interrupt (DICR)
// ===========================================================================

void Dma::write32(u32 offset, u32 val) {
    int ch = static_cast<int>((offset / 8) & 7);

    if (ch < NUM_CHANNELS) {
        int reg = (offset / 4) % 2;  // 0=MADR/BCR, 1=CHCR
        switch (reg) {
            case 0: {
                // Alternate between MADR and BCR based on offset.
                // Actually, each channel occupies 0x10 bytes:
                //   +0x00: MADR (base address)
                //   +0x04: BCR  (block control)
                //   +0x08: CHCR (channel control)
                int sub = (offset / 4) % 3;
                switch (sub) {
                    case 0: channels_[ch].base_addr = val & 0x00FFFFFF; break;
                    case 1: channels_[ch].block_ctrl = val; break;
                    case 2:
                        channels_[ch].channel_ctrl = val;
                        // Bit 24 = start trigger
                        if (val & (1u << 24)) {
                            trigger(ch);
                        }
                        break;
                }
                break;
            }
            case 1: {
                // CHCR is at offset+8 from channel base.
                channels_[ch].channel_ctrl = val;
                if (val & (1u << 24)) {
                    trigger(ch);
                }
                break;
            }
        }
        return;
    }

    // DMA control register (DPCR) at offset 0x70
    if (offset == 0x70) {
        control_ = val;
        // Check if any channels should be triggered by priority.
        for (int i = 0; i < NUM_CHANNELS; i++) {
            u32 priority = (control_ >> (i * 4)) & 0x07;
            if (priority == 0) continue;  // Disabled
            u32 chcr = channels_[i].channel_ctrl;
            if ((chcr & (1u << 24)) && (chcr & 0x01000000) == 0) {
                // Start bit set, transfer not yet done.
                // Don't auto-trigger here — the game sets the start bit explicitly.
            }
        }
        return;
    }

    // DMA interrupt register (DICR) at offset 0x74
    if (offset == 0x74) {
        // Bits 23:16 = IRQ enable
        // Bits 15:8  = IRQ flags (write 1 to clear)
        u32 new_enable = (val >> 16) & 0xFF;
        u32 ack = (val >> 8) & 0xFF;
        irq_enable_ = new_enable;
        irq_flags_ &= ~ack;
        irq_master_ = (val >> 23) & 1;
        irq_flag_29_ = (val >> 31) & 1;
        return;
    }
}

u32 Dma::read32(u32 offset) {
    int ch = static_cast<int>((offset / 8) & 7);

    if (ch < NUM_CHANNELS) {
        int sub = (offset / 4) % 3;
        switch (sub) {
            case 0: return channels_[ch].base_addr;
            case 1: return channels_[ch].block_ctrl;
            case 2: {
                u32 chcr = channels_[ch].channel_ctrl;
                // Clear the start bit on read (some games rely on this).
                channels_[ch].channel_ctrl = chcr & ~(1u << 24);
                return chcr;
            }
        }
    }

    if (offset == 0x70) return control_;
    if (offset == 0x74) {
        u32 val = irq_enable_ << 16;
        val |= irq_flags_ << 8;
        val |= irq_master_ << 23;
        val |= irq_flag_29_ << 31;
        if (irq_active()) val |= (1u << 31);
        return val;
    }

    return 0;
}

// ===========================================================================
//  IRQ
// ===========================================================================

void Dma::ack_irq(u32 bits) {
    // Called when the CPU writes to I_STAT (0x1F801070) to acknowledge IRQs.
    // The bits correspond to the I_MASK/I_STAT interrupt controller.
    // This is actually the interrupt controller, not DMA directly.
    // DMA signals IRQ2 (bit 2 of I_STAT) when a DMA transfer completes.
    (void)bits;
}

void Dma::set_irq_mask(u32 mask) {
    // Called when CPU writes to I_MASK (0x1F801074).
    (void)mask;
}

bool Dma::irq_active() const {
    if (irq_flag_29_) return true;
    return (irq_flags_ & irq_enable_) != 0;
}

// ===========================================================================
//  Trigger
// ===========================================================================

void Dma::trigger(u32 channel) {
    if (channel >= NUM_CHANNELS) return;

    auto& ch = channels_[channel];
    u32 chcr = ch.channel_ctrl;

    // Direction: bit 0 = to RAM (device read), 1 = from RAM (device write)
    bool to_ram = (chcr & 1) == 0;
    // Sync mode
    u32 sync = (chcr >> 9) & 3;

    switch (sync) {
        case 0: // Start immediately (linked list mode for GPU)
            if (channel == GPU && !to_ram) {
                transfer_linked_list(static_cast<int>(channel));
            } else {
                transfer_block(static_cast<int>(channel));
            }
            break;
        case 1: // Sync block
            transfer_block(static_cast<int>(channel));
            break;
        case 2: // Sync linked list
            transfer_linked_list(static_cast<int>(channel));
            break;
        default:
            LOG_DEBUG("DMA ch{} unknown sync mode {}", channel, sync);
            break;
    }

    // Clear start bit.
    ch.channel_ctrl &= ~(1u << 24);

    // Set completion flag.
    irq_flags_ |= (1u << channel);
}

// ===========================================================================
//  Block transfer
// ===========================================================================

void Dma::transfer_block(int ch) {
    auto& chan = channels_[ch];
    u32 addr = chan.base_addr;
    u32 bcr = chan.block_ctrl;
    u32 chcr = chan.channel_ctrl;

    bool to_ram = (chcr & 1) == 0;  // 0 = device -> RAM
    u32 sync = (chcr >> 9) & 3;

    u32 block_size, block_count;
    if (sync == 1) {
        // Block sync: BCR = (block_count << 16) | block_size
        block_size = bcr & 0xFFFF;
        block_count = (bcr >> 16) & 0xFFFF;
    } else if (sync == 0) {
        // Immediate: BCR = total words
        block_size = bcr & 0xFFFF;
        block_count = 1;
    } else {
        block_size = 1;
        block_count = 1;
    }

    u32 total_words = block_size * block_count;

    for (u32 i = 0; i < total_words; i++) {
        u32 phys_addr = addr & 0x1FFFFF;
        u32 word = 0;

        if (to_ram) {
            // Device -> RAM
            switch (ch) {
                case CDROM:
                    if (cdrom_) {
                        cdrom_->dma_read(reinterpret_cast<u8*>(&word), 4);
                    }
                    break;
                default:
                    break;
            }
            if (bus_) bus_->write32(phys_addr, word);
        } else {
            // RAM -> Device
            if (bus_) word = bus_->read32(phys_addr);

            switch (ch) {
                case GPU:
                    if (gpu_) {
                        u8 bytes[4];
                        std::memcpy(bytes, &word, 4);
                        gpu_->dma_write(bytes, 1);
                    }
                    break;
                case SPU:
                    if (spu_) {
                        u8 bytes[4];
                        std::memcpy(bytes, &word, 4);
                        spu_->dma_write(bytes, 4);
                    }
                    break;
                case OTC: {
                    // Ordering table clear: write addr to previous position.
                    u32 write_addr = (addr - 4) & 0x1FFFFF;
                    u32 terminator = 0x00FFFFFFu;
                    if (bus_) bus_->write32(write_addr, terminator);
                    break;
                }
                default:
                    break;
            }
        }

        addr += 4;
    }
}

// ===========================================================================
//  Linked list transfer
// ===========================================================================

void Dma::transfer_linked_list(int ch) {
    auto& chan = channels_[ch];
    u32 addr = chan.base_addr & 0x1FFFFF;
    u32 bcr = chan.block_ctrl;

    if (ch == GPU && gpu_) {
        gpu_->dma_linked_list(addr, bus_);
        return;
    }

    // Generic linked list transfer.
    u32 words_remaining = bcr & 0xFFFF;
    while (words_remaining > 0) {
        if (!bus_) break;
        u32 header = bus_->read32(addr);
        u32 next = header & 0x00FFFFFF;
        u32 count = (header >> 24) & 0xFF;
        if (count == 0) break;  // End of list.

        addr += 4;
        for (u32 i = 0; i < count; i++) {
            u32 word = bus_->read32(addr);
            switch (ch) {
                case GPU:
                    if (gpu_) gpu_->write_gp0(word);
                    break;
                default:
                    break;
            }
            addr += 4;
        }

        words_remaining -= (count + 1);
        addr = next & 0x1FFFFF;

        // Terminate if high bit set.
        if (next & 0x800000) break;
    }
}

// ===========================================================================
//  Timing
// ===========================================================================

void Dma::tick(u32 cycles) {
    (void)cycles;
    // DMA transfers are currently synchronous (block during trigger).
    // For more accurate timing, we could make them asynchronous here.
}

} // namespace yaps1