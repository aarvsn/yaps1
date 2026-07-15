/// @file memcard.cpp
/// @brief Memory card implementation — 128 KB save data with auto-create.

#include "memcards/memcard.h"
#include "common/logger.h"
#include <fstream>
#include <cstring>

namespace yaps1 {

// ===========================================================================
//  Load / Save
// ===========================================================================

Result<bool> MemoryCard::load(const Path& path, int slot) {
    file_path_ = path;
    slot_ = slot;

    if (std::filesystem::exists(path)) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            return std::unexpected(Error{"Cannot open memory card: " + path.string()});
        }
        auto size = f.tellg();
        f.seekg(0);
        if (size == CARD_SIZE) {
            f.read(reinterpret_cast<char*>(data_.data()), CARD_SIZE);
            present_ = true;
            LOG_INFO("Memory card {} loaded: {}", slot, path.filename().string());
            return true;
        }
        // Wrong size — reformat.
        LOG_WARN("Memory card has wrong size, reformatting");
    }

    // Create a new, formatted memory card.
    format();
    present_ = true;

    // Write to disk.
    auto save_result = save();
    if (!save_result) return save_result;

    LOG_INFO("Memory card {} created: {}", slot, path.filename().string());
    return true;
}

Result<bool> MemoryCard::save() {
    if (!present_) return true;

    std::ofstream f(file_path_, std::ios::binary);
    if (!f.is_open()) {
        return std::unexpected(Error{"Cannot save memory card: " + file_path_.string()});
    }
    f.write(reinterpret_cast<const char*>(data_.data()), CARD_SIZE);
    if (!f) {
        return std::unexpected(Error{"Write error on memory card: " + file_path_.string()});
    }
    return true;
}

// ===========================================================================
//  Format
// ===========================================================================

void MemoryCard::format() {
    data_.fill(0xFF);

    // Frame 0: Memory card header.
    // Byte 0: 'M' (0x4D), Byte 1: 'C' (0x43)
    data_[0] = 0x4D;
    data_[1] = 0x43;
    // Byte 8-11: BIOS checksum area.
    // Byte 127: XOR checksum of bytes 0-127.
    u8 xor_sum = 0;
    for (int i = 0; i < 127; i++) {
        xor_sum ^= data_[i];
    }
    data_[127] = xor_sum;

    // Frame 1: Directory (frame 1-15).
    // Frame 1, byte 0: 0x00 = free, 0x51 = first sector of a save block.
    data_[FRAME_SIZE] = 0xA0;  // Directory header.

    // Sector allocation table (frames 36-42).
    // All sectors marked as free (0xFF).
    for (u32 frame = 36; frame < 42; frame++) {
        for (u32 i = 0; i < FRAME_SIZE; i++) {
            data_[frame * FRAME_SIZE + i] = 0xFF;
        }
    }

    // Broken sector list (frames 42-43) — all free.
    // (already 0xFF from fill)
}

// ===========================================================================
//  Transfer protocol
// ===========================================================================

void MemoryCard::begin_transfer(u8 cmd, u8 sector) {
    switch (cmd) {
        case 0x52: // Read
            tx_state_ = TxState::Read;
            sector_addr_ = sector * FRAME_SIZE;
            byte_offset_ = 0;
            break;
        case 0x57: // Write
            tx_state_ = TxState::Write;
            sector_addr_ = sector * FRAME_SIZE;
            byte_offset_ = 0;
            checksum_ = 0;
            break;
        case 0x53: // ID
            tx_state_ = TxState::Id;
            byte_offset_ = 0;
            break;
        default:
            tx_state_ = TxState::Idle;
            break;
    }
}

u8 MemoryCard::read_byte() {
    switch (tx_state_) {
        case TxState::Read:
            if (byte_offset_ < FRAME_SIZE) {
                return data_[sector_addr_ + byte_offset_++];
            }
            tx_state_ = TxState::Idle;
            return 0xFF;
        case TxState::Id:
            if (byte_offset_ == 0) { byte_offset_++; return 0x5A; }
            if (byte_offset_ == 1) { byte_offset_++; return 0x5A; }
            if (byte_offset_ == 2) { byte_offset_++; return 0x00; }
            if (byte_offset_ == 3) { byte_offset_++; return 0x00; }
            if (byte_offset_ == 4) { byte_offset_++; return 0x80; }
            tx_state_ = TxState::Idle;
            return 0xFF;
        default:
            return 0xFF;
    }
}

void MemoryCard::write_byte(u8 val) {
    switch (tx_state_) {
        case TxState::Write:
            if (byte_offset_ < FRAME_SIZE) {
                data_[sector_addr_ + byte_offset_] = val;
                checksum_ ^= val;
                byte_offset_++;
                if (byte_offset_ >= FRAME_SIZE) {
                    // Write complete. Optionally auto-save.
                    tx_state_ = TxState::Idle;
                    (void)save();  // Auto-save after each sector write.
                }
            }
            break;
        default:
            break;
    }
}

bool MemoryCard::transfer_done() const {
    return tx_state_ == TxState::Idle;
}

} // namespace yaps1