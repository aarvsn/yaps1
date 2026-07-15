/// @file cdrom.cpp
/// @brief CD-ROM controller implementation.

#include "cdrom/cdrom.h"
#include "common/types.h"
#include "common/logger.h"
#include <cstring>
#include <algorithm>

namespace yaps1 {

// CD-ROM sector read time: ~33.87 MHz / (75 sectors/sec) = ~451583 cycles/sector
static constexpr u32 CYCLES_PER_SECTOR = 451583;

Cdrom::Cdrom() { reset(); }

void Cdrom::reset() {
    status_reg_  = 0x10;  // Shell open, no motor
    command_reg_ = 0;
    int_enable_  = 0;
    int_flag_    = 0;
    response_fifo_.fill(0);
    response_count_ = 0;
    response_read_ = 0;
    data_fifo_.fill(0);
    data_count_ = 0;
    data_read_ = 0;
    data_ready_ = false;
    current_sector_ = 0;
    sector_count_ = 0;
    reading_ = false;
    read_timer_ = 0;
    cycle_acc_ = 0;
    irq_pending_ = false;
}

// ===========================================================================
//  Disc loading
// ===========================================================================

Result<bool> Cdrom::load_disc(const Path& path) {
    std::string ext = path.extension().string();
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".cue") {
        // Parse CUE file to find the BIN.
        std::ifstream cue(path);
        if (!cue.is_open()) {
            return std::unexpected(Error{"Cannot open CUE file: " + path.string()});
        }
        std::string line;
        std::string bin_file;
        while (std::getline(cue, line)) {
            // Look for FILE directive.
            auto pos = line.find("FILE");
            if (pos != std::string::npos) {
                // Extract filename between quotes.
                auto q1 = line.find('"', pos);
                auto q2 = line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    bin_file = line.substr(q1 + 1, q2 - q1 - 1);
                }
                // Check for BINARY / MOTOROLA
                break;
            }
        }
        if (bin_file.empty()) {
            return std::unexpected(Error{"No FILE directive found in CUE file"});
        }

        // Resolve BIN path relative to CUE directory.
        Path bin_path = path.parent_path() / bin_file;
        std::ifstream bin(bin_path, std::ios::binary | std::ios::ate);
        if (!bin.is_open()) {
            return std::unexpected(Error{"Cannot open BIN file: " + bin_path.string()});
        }
        disc_size_ = static_cast<u32>(bin.tellg());
        bin.seekg(0);
        disc_data_.resize(disc_size_);
        bin.read(reinterpret_cast<char*>(disc_data_.data()), disc_size_);
        track_offset_ = 0;  // Usually 0 for single-track
    }
    else if (ext == ".bin" || ext == ".img" || ext == ".iso") {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            return std::unexpected(Error{"Cannot open disc image: " + path.string()});
        }
        disc_size_ = static_cast<u32>(f.tellg());
        f.seekg(0);
        disc_data_.resize(disc_size_);
        f.read(reinterpret_cast<char*>(disc_data_.data()), disc_size_);
        track_offset_ = 0;
    }
    else {
        return std::unexpected(Error{"Unsupported disc format: " + ext});
    }

    // Determine sector size based on disc size.
    // Mode 1: 2048 bytes/sector
    // Mode 2: 2336 bytes/sector (raw)
    // Standard PS1 discs use 2352 bytes/sector (raw with headers).
    // If size is a multiple of 2352, assume raw.
    if (disc_size_ % 2352 == 0) {
        sector_size_ = 2352;
    } else if (disc_size_ % 2048 == 0) {
        sector_size_ = 2048;
    } else {
        sector_size_ = 2352;  // Default to raw.
    }

    disc_loaded_ = true;
    status_reg_ = 0x02;  // Motor on, disc spinning
    LOG_INFO("Disc loaded: {} ({} bytes, {}-byte sectors)",
             path.filename().string(), disc_size_, sector_size_);
    return true;
}

// ===========================================================================
//  Register read/write
// ===========================================================================

void Cdrom::write32(u32 offset, u32 val) {
    // offset is relative to 0x1F801800.
    // The CD-ROM uses byte/halfword access primarily, but we handle word writes.
    u8 reg = (offset >> 2) & 3;

    switch (reg) {
        case 0: // Status / Command register
            // Write = issue command (when bit 7 = 1)
            if (val & 0x80) {
                command_reg_ = val & 0xFF;
                execute_command(command_reg_);
            } else {
                // Write to status register (bit manipulation).
                status_reg_ = val & 0xFF;
            }
            break;
        case 2: // Data FIFO (write — not typically used)
            break;
        case 3: // Interrupt enable/acknowledge
            int_enable_ = val & 0xFF;
            // Writing 1s in bits 5:0 acknowledges those interrupts.
            int_flag_ &= ~(val & 0x3F);
            if ((int_flag_ & int_enable_) == 0) {
                irq_pending_ = false;
            }
            break;
    }
}

u32 Cdrom::read32(u32 offset) {
    u8 reg = (offset >> 2) & 3;

    switch (reg) {
        case 0: { // Status / Command
            u8 s = status_reg_;
            if (data_ready_)   s |= 0x20;
            if (data_count_ > 0) s |= 0x20;
            if (irq_pending_)  s |= 0x04;
            return s | (static_cast<u32>(int_flag_ & int_enable_) << 8);
        }
        case 1: { // Response FIFO
            if (response_read_ < response_count_) {
                u8 val = response_fifo_[response_read_++];
                if (response_read_ >= response_count_) {
                    response_count_ = 0;
                    response_read_ = 0;
                }
                return val;
            }
            return 0;
        }
        case 2: { // Data FIFO
            if (data_read_ < data_count_) {
                u8 b0 = data_fifo_[data_read_++];
                u8 b1 = (data_read_ < data_count_) ? data_fifo_[data_read_++] : 0;
                u8 b2 = (data_read_ < data_count_) ? data_fifo_[data_read_++] : 0;
                u8 b3 = (data_read_ < data_count_) ? data_fifo_[data_read_++] : 0;
                if (data_read_ >= data_count_) {
                    data_ready_ = false;
                    data_count_ = 0;
                    data_read_ = 0;
                    if (reading_ && sector_count_ > 0) {
                        read_sector(current_sector_);
                        sector_count_--;
                    }
                }
                return (static_cast<u32>(b3) << 24) |
                       (static_cast<u32>(b2) << 16) |
                       (static_cast<u32>(b1) << 8)  |
                        static_cast<u32>(b0);
            }
            return 0;
        }
        case 3: // Interrupt enable/status
            return int_enable_ | (static_cast<u32>(int_flag_) << 8);
    }
    return 0;
}

// ===========================================================================
//  DMA read (CD -> RAM)
// ===========================================================================

void Cdrom::dma_read(u8* dst, u32 bytes) {
    u32 to_copy = std::min(bytes, static_cast<u32>(data_count_ - data_read_));
    std::memcpy(dst, &data_fifo_[data_read_], to_copy);
    data_read_ += to_copy;

    if (data_read_ >= data_count_) {
        data_ready_ = false;
        data_count_ = 0;
        data_read_ = 0;
        if (reading_ && sector_count_ > 0) {
            read_sector(current_sector_);
            sector_count_--;
        }
    }
}

// ===========================================================================
//  Internal helpers
// ===========================================================================

void Cdrom::push_response(u8 val) {
    if (response_count_ < MAX_RESPONSE) {
        response_fifo_[response_count_++] = val;
    }
}

void Cdrom::push_data(const u8* sector, u32 size) {
    u32 copy = std::min(size, static_cast<u32>(DATA_FIFO_SIZE));
    std::memcpy(data_fifo_.data(), sector, copy);
    data_count_ = copy;
    data_read_ = 0;
    data_ready_ = true;
}

void Cdrom::set_interrupt(u8 irq) {
    int_flag_ |= irq;
    if (int_flag_ & int_enable_) {
        irq_pending_ = true;
    }
}

void Cdrom::read_sector(u32 sector) {
    if (!disc_loaded_) return;

    u32 offset;
    const u8* src;

    if (sector_size_ == 2352) {
        // Raw: skip 24-byte header + 8-byte subheader to get 2048 bytes of data.
        offset = sector * 2352;
        if (offset + 2352 > disc_size_) {
            set_interrupt(0x05);  // Error
            return;
        }
        src = &disc_data_[offset + 24];  // Skip sync+header+subheader
        push_data(src, 2048);
    } else {
        offset = sector * sector_size_;
        if (offset + sector_size_ > disc_size_) {
            set_interrupt(0x05);
            return;
        }
        src = &disc_data_[offset];
        push_data(src, sector_size_);
    }

    current_sector_++;
    set_interrupt(0x01);  // Data ready
}

// ===========================================================================
//  Command execution
// ===========================================================================

void Cdrom::execute_command(u8 cmd) {
    LOG_DEBUG("CDROM command: {:02X}", cmd);

    switch (cmd) {
        case 0x01: { // GetStat
            push_response(status_reg_);
            set_interrupt(0x03);  // Complete
            break;
        }
        case 0x02: { // Setloc
            // Parameters come via subsequent writes.
            // For simplicity, we'll handle it when the sector count is set.
            set_interrupt(0x03);
            break;
        }
        case 0x06: { // ReadN (read without retry)
            reading_ = true;
            sector_count_ = 1;
            read_sector(current_sector_);
            break;
        }
        case 0x08: { // Stop (stop the disc motor)
            reading_ = false;
            status_reg_ = 0x00;  // Motor stopped
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x09: { // Pause
            reading_ = false;
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x0A: { // Init
            reset();
            status_reg_ = 0x02;
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x0E: { // Setmode
            // Mode byte follows. We just ACK it.
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x15: { // SeekL
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x19: { // Test
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x1A: { // GetID
            // Return disc ID info.
            push_response(0x00);
            push_response(0x00);
            push_response(0x20);  // Audio disc
            set_interrupt(0x03);
            break;
        }
        case 0x1E: { // ReadTOC
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        default:
            LOG_DEBUG("Unknown CDROM command {:02X}", cmd);
            push_response(0x40);  // Illegal command
            set_interrupt(0x05);  // Error
            break;
    }
}

// ===========================================================================
//  Timing
// ===========================================================================

void Cdrom::tick(u32 cycles) {
    cycle_acc_ += cycles;
    // Process sector reads at the CD-ROM's natural rate.
    while (cycle_acc_ >= CYCLES_PER_SECTOR) {
        cycle_acc_ -= CYCLES_PER_SECTOR;
        // If we're in a continuous read, advance.
        if (reading_ && data_count_ == 0 && sector_count_ > 0) {
            read_sector(current_sector_);
            sector_count_--;
        }
    }
}

} // namespace yaps1