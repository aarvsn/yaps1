#pragma once

/// @file cdrom.h
/// @brief PS1 CD-ROM controller — reads BIN/CUE/ISO disc images.

#include "common/types.h"
#include <array>
#include <string>
#include <vector>
#include <fstream>

namespace yaps1 {

class BusInterface;

/// The PS1 CD-ROM controller handles disc reads, XA audio, and subchannel data.
class Cdrom {
public:
    Cdrom();
    ~Cdrom() = default;

    void reset();
    void set_bus(BusInterface* bus) { bus_ = bus; }

    // ---- Load a disc image ----
    /// Load a .bin/.cue or raw .bin file.
    Result<bool> load_disc(const Path& path);
    Result<bool> load_chd(const Path& path);

    // ---- Register interface ----
    void write32(u32 offset, u32 val);
    [[nodiscard]] u32 read32(u32 offset);

    // ---- DMA interface ----
    void dma_read(u8* dst, u32 bytes);

    // ---- Timing ----
    void tick(u32 cycles);

    // ---- Interrupt ----
    [[nodiscard]] bool irq_pending() const { return irq_pending_; }
    void clear_irq() { irq_pending_ = false; }
    u32 irq_bit() const { return 2; }  // IRQ2 in the I_MASK

    // ---- Status ----
    [[nodiscard]] bool disc_loaded() const { return disc_loaded_; }

private:
    // ---- CD-ROM registers (0x1F801800) ----
    //   +0x00  Status / Command
    //   +0x04  Response FIFO
    //   +0x08  Data FIFO
    //   +0x0C  Interrupt enable/status

    u8  status_reg_  = 0;   // Status byte
    u8  command_reg_ = 0;   // Current/last command
    u8  int_enable_  = 0;   // Interrupt enable
    u8  int_flag_    = 0;   // Interrupt flags

    // ---- Parameter FIFO ----
    static constexpr int MAX_PARAM = 16;
    std::array<u8, MAX_PARAM> param_fifo_{};
    int param_count_ = 0;
    int param_read_  = 0;

    // ---- Response FIFO ----
    static constexpr int MAX_RESPONSE = 16;
    std::array<u8, MAX_RESPONSE> response_fifo_{};
    int response_count_ = 0;
    int response_read_  = 0;

    // ---- Data FIFO ----
    static constexpr int DATA_FIFO_SIZE = 2336;
    std::array<u8, DATA_FIFO_SIZE> data_fifo_{};
    int data_count_ = 0;
    int data_read_  = 0;
    bool data_ready_ = false;

    // ---- Sector state ----
    u32  current_sector_  = 0;
    u32  sector_count_    = 0;
    bool reading_         = false;
    u32  read_timer_      = 0;

    // ---- Command parameters ----
    u32  cmd_sector_lba_  = 0;
    bool cmd_sector_set_  = false;

    // ---- Disc data ----
    bool disc_loaded_ = false;
    std::vector<u8> disc_data_;   // Raw disc image data
    u32  disc_size_ = 0;
    u32  track_offset_ = 0;       // Offset for track 1 data (past pregap)
    u32  sector_size_  = 2048;    // Mode 2 data

    // ---- Timing ----
    u32 cycle_acc_ = 0;

    // ---- IRQ ----
    bool irq_pending_ = false;

    // ---- Internal helpers ----
    void push_response(u8 val);
    void push_data(const u8* sector, u32 size);
    void set_interrupt(u8 irq);
    void execute_command(u8 cmd);
    void read_sector(u32 sector);

    BusInterface* bus_ = nullptr;
};

} // namespace yaps1