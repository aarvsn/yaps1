#pragma once

/// @file memcard.h
/// @brief PS1 Memory Card — 128 KB save data, auto-created.

#include "common/types.h"
#include <array>
#include <string>

namespace yaps1 {

/// A PS1 memory card holds 128 KB of save data in 16-byte frames.
/// Data is stored as a .mcd file on disk.
class MemoryCard {
public:
    static constexpr u32 CARD_SIZE    = 128 * 1024;  // 128 KB
    static constexpr u32 FRAME_SIZE   = 128;         // Bytes per sector
    static constexpr u32 NUM_FRAMES   = CARD_SIZE / FRAME_SIZE;

    MemoryCard() = default;
    ~MemoryCard() = default;

    /// Load a memory card file. If it doesn't exist, create a formatted one.
    Result<bool> load(const Path& path, int slot);

    /// Save the memory card to disk.
    Result<bool> save();

    /// Read/write access for the Joy I/O protocol.
    void begin_transfer(u8 cmd, u8 sector);
    u8 read_byte();
    void write_byte(u8 val);
    bool transfer_done() const;

    /// Get the raw data pointer (for save-states).
    [[nodiscard]] u8* data() { return data_.data(); }
    [[nodiscard]] const u8* data() const { return data_.data(); }

    /// Check if card is present.
    [[nodiscard]] bool present() const { return present_; }

    /// Get slot number.
    [[nodiscard]] int slot() const { return slot_; }

private:
    std::array<u8, CARD_SIZE> data_{};
    Path file_path_;
    int  slot_ = 0;
    bool present_ = false;

    // Transfer state
    enum class TxState { Idle, Read, Write, Id, Directory } tx_state_ = TxState::Idle;
    u32 sector_addr_ = 0;
    u32 byte_offset_ = 0;
    u8  checksum_ = 0;

    /// Format a blank memory card with the standard header.
    void format();
};

} // namespace yaps1