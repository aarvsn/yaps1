#include "cdrom/cdrom.h"
#include "common/types.h"
#include "common/logger.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#ifdef YAPS1_HAS_ZLIB
#include <zlib.h>
#endif

namespace yaps1 {

static constexpr u32 CYCLES_PER_SECTOR = 451583;

Cdrom::Cdrom() { reset(); }

void Cdrom::reset() {
    status_reg_  = 0x10;
    command_reg_ = 0;
    int_enable_  = 0;
    int_flag_    = 0;
    param_fifo_.fill(0);
    param_count_ = 0;
    param_read_  = 0;
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
    cmd_sector_lba_ = 0;
    cmd_sector_set_ = false;
}

// ===========================================================================
//  Disc loading
// ===========================================================================

Result<bool> Cdrom::load_disc(const Path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".cue") {
        std::ifstream cue(path);
        if (!cue.is_open()) {
            return std::unexpected(Error{"Cannot open CUE file: " + path.string()});
        }
        std::string line;
        std::string bin_file;
        while (std::getline(cue, line)) {
            auto pos = line.find("FILE");
            if (pos != std::string::npos) {
                auto q1 = line.find('"', pos);
                auto q2 = line.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    bin_file = line.substr(q1 + 1, q2 - q1 - 1);
                }
                break;
            }
        }
        if (bin_file.empty()) {
            return std::unexpected(Error{"No FILE directive found in CUE file"});
        }
        Path bin_path = path.parent_path() / bin_file;
        std::ifstream bin(bin_path, std::ios::binary | std::ios::ate);
        if (!bin.is_open()) {
            return std::unexpected(Error{"Cannot open BIN file: " + bin_path.string()});
        }
        disc_size_ = static_cast<u32>(bin.tellg());
        bin.seekg(0);
        disc_data_.resize(disc_size_);
        bin.read(reinterpret_cast<char*>(disc_data_.data()), disc_size_);
        track_offset_ = 0;
    }
    else if (ext == ".bin" || ext == ".img" || ext == ".iso" || ext == ".chd") {
        if (ext == ".chd") {
            return load_chd(path);
        }
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

    if (disc_size_ % 2352 == 0) {
        sector_size_ = 2352;
    } else if (disc_size_ % 2048 == 0) {
        sector_size_ = 2048;
    } else {
        sector_size_ = 2352;
    }

    disc_loaded_ = true;
    status_reg_ = 0x02;
    LOG_INFO("Disc loaded: {} ({} bytes, {}-byte sectors)",
             path.filename().string(), disc_size_, sector_size_);
    return true;
}

// ===========================================================================
//  Register read/write
// ===========================================================================

void Cdrom::write32(u32 offset, u32 val) {
    u8 reg = (offset >> 2) & 3;

    switch (reg) {
        case 0: // Status / Command register
            if (val & 0x80) {
                command_reg_ = val & 0xFF;
                execute_command(command_reg_);
            } else {
                status_reg_ = val & 0xFF;
                if (param_count_ < MAX_PARAM) {
                    param_fifo_[param_count_++] = val & 0xFF;
                }
            }
            break;
        case 2: // Data FIFO (write)
            break;
        case 3: // Interrupt enable/acknowledge
            int_enable_ = val & 0xFF;
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
        case 0: {
            u8 s = status_reg_;
            if (data_ready_)   s |= 0x20;
            if (irq_pending_)  s |= 0x04;
            return s | (static_cast<u32>(int_flag_ & int_enable_) << 8);
        }
        case 1: {
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
        case 2: {
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
        case 3:
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
        offset = sector * 2352;
        if (offset + 2352 > disc_size_) {
            set_interrupt(0x05);
            return;
        }
        src = &disc_data_[offset + 24];
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
    set_interrupt(0x01);
}

// ===========================================================================
//  Command execution
// ===========================================================================

void Cdrom::execute_command(u8 cmd) {
    LOG_DEBUG("CDROM command: {:02X}", cmd);

    switch (cmd) {
        case 0x01: { // GetStat
            push_response(status_reg_);
            set_interrupt(0x03);
            break;
        }
        case 0x02: { // Setloc
            // Parameters: minute, second, sector (3 bytes in BCD)
            if (param_count_ >= 3) {
                u8 minute = param_fifo_[0];
                u8 second = param_fifo_[1];
                u8 sector = param_fifo_[2];
                u32 bcd_min = ((minute >> 4) * 10) + (minute & 0x0F);
                u32 bcd_sec = ((second >> 4) * 10) + (second & 0x0F);
                u32 bcd_sec2 = ((sector >> 4) * 10) + (sector & 0x0F);
                cmd_sector_lba_ = (bcd_min * 60 + bcd_sec) * 75 + bcd_sec2;
                cmd_sector_lba_ -= 150;
                cmd_sector_set_ = true;
            }
            param_count_ = 0;
            param_read_ = 0;
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x03: { // ReadS (subchannel)
            reading_ = true;
            sector_count_ = 1;
            if (cmd_sector_set_) {
                current_sector_ = cmd_sector_lba_;
            }
            read_sector(current_sector_);
            break;
        }
        case 0x06: { // ReadN
            reading_ = true;
            sector_count_ = 1;
            if (cmd_sector_set_) {
                current_sector_ = cmd_sector_lba_;
                cmd_sector_set_ = false;
            }
            read_sector(current_sector_);
            break;
        }
        case 0x08: { // Stop
            reading_ = false;
            status_reg_ = 0x00;
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
        case 0x0C: { // MotorOn
            status_reg_ = 0x02;
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x0D: { // ReadCNF (read config)
            push_response(0x00);
            push_response(0x08);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            push_response(0x40);
            set_interrupt(0x03);
            break;
        }
        case 0x0E: { // Setmode
            if (param_count_ >= 1) {
                u8 mode = param_fifo_[0];
                // Store mode byte: bit 0=CDDA, bit 1=auto-pause, bit 2=report, bit 3=XA filter
                param_count_ = 0;
                param_read_ = 0;
                (void)mode;
            }
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x10: { // GetlocL
            push_response(0x00);
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x12: { // GetlocP
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x15: { // SeekL
            if (cmd_sector_set_) {
                current_sector_ = cmd_sector_lba_;
                cmd_sector_set_ = false;
            }
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x16: { // SeekP
            if (cmd_sector_set_) {
                current_sector_ = cmd_sector_lba_;
                cmd_sector_set_ = false;
            }
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x19: { // Test
            u8 sub = (param_count_ > 0) ? param_fifo_[0] : 0;
            param_count_ = 0;
            param_read_ = 0;
            switch (sub) {
                case 0x20: // Return date / version
                    push_response(0x95);
                    push_response(0x01);
                    push_response(0x01);
                    push_response(0x01);
                    push_response(0x01);
                    push_response(0x01);
                    push_response(0x01);
                    push_response(0x01);
                    break;
                case 0x03:
                default:
                    push_response(0x00);
                    break;
            }
            set_interrupt(0x03);
            break;
        }
        case 0x1A: { // GetID
            if (!disc_loaded_) {
                push_response(0x40);
                push_response(0x80);
                push_response(0x00);
                push_response(0x00);
                set_interrupt(0x05);
                break;
            }
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            push_response(0x20);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x1B: { // ReadTOC (in MMC format)
            push_response(0x00);
            set_interrupt(0x03);
            break;
        }
        case 0x1E: { // ReadTOC (in CD-ROM format)
            u8 track = (param_count_ > 0) ? param_fifo_[0] : 0;
            param_count_ = 0;
            param_read_ = 0;

            push_response(0x01);
            push_response(0x00);
            push_response(0x00);
            push_response(0x00);

            if (track == 0 || track == 1) {
                push_response(0x01);
                push_response(0x00);
                push_response(0x00);
                push_response(0x00);
            }

            push_response(0xAA);
            push_response(0x00);
            push_response(0x00);
            u32 leadout_lba = (disc_size_ / sector_size_) - 150;
            u32 leadout_min = (leadout_lba / 75) / 60;
            u32 leadout_sec = (leadout_lba / 75) % 60;
            u32 leadout_frame = leadout_lba % 75;
            push_response(static_cast<u8>(((leadout_min / 10) << 4) | (leadout_min % 10)));
            push_response(static_cast<u8>(((leadout_sec / 10) << 4) | (leadout_sec % 10)));
            push_response(static_cast<u8>(((leadout_frame / 10) << 4) | (leadout_frame % 10)));

            set_interrupt(0x03);
            break;
        }
        default:
            LOG_DEBUG("Unknown CDROM command {:02X}", cmd);
            push_response(0x40);
            set_interrupt(0x05);
            break;
    }
}

// ===========================================================================
//  CHD loading
// ===========================================================================

Result<bool> Cdrom::load_chd(const Path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return std::unexpected(Error{"Cannot open CHD file: " + path.string()});
    }

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, "MComprHD", 8) != 0) {
        return std::unexpected(Error{"Not a valid CHD file"});
    }

    u32 version;
    f.read(reinterpret_cast<char*>(&version), 4);
    version = (version >> 24) | ((version >> 8) & 0xFF00) |
              ((version << 8) & 0xFF0000) | (version << 24);

    if (version < 1 || version > 5) {
        return std::unexpected(Error{"Unsupported CHD version: " + std::to_string(version)});
    }

    u64 total_bytes = 0;
    u32 hunk_size = 0;
    [[maybe_unused]] u32 unit_size = 0;
    [[maybe_unused]] u64 rawmap_offset = 0;
    u64 map_offset = 0;
    u64 meta_offset = 0;

    if (version >= 5) {
        u32 compressor;
        f.seekg(32);
        f.read(reinterpret_cast<char*>(&compressor), 4);
        f.seekg(40);
        f.read(reinterpret_cast<char*>(&hunk_size), 4);
        f.read(reinterpret_cast<char*>(&total_bytes), 8);
        f.read(reinterpret_cast<char*>(&meta_offset), 8);
        f.read(reinterpret_cast<char*>(&map_offset), 8);

        u32 total_hunks = static_cast<u32>((total_bytes + hunk_size - 1) / hunk_size);

        std::vector<u8> compressed;
        disc_data_.resize(static_cast<u32>(total_bytes));
        disc_size_ = static_cast<u32>(total_bytes);

        f.seekg(static_cast<i64>(map_offset));
        std::vector<u8> map(total_hunks * 12);
        f.read(reinterpret_cast<char*>(map.data()), map.size());

        for (u32 i = 0; i < total_hunks; i++) {
            u64 offset = 0;
            u32 crc32 = 0;

            std::memcpy(&offset, &map[i * 12], 8);
            std::memcpy(&crc32, &map[i * 12 + 8], 4);

            u64 block_offset = offset & 0x3FFFFFFFFFFFFFFFULL;
            u8 compression_type = static_cast<u8>((offset >> 62) & 3);

            u32 dst_offset = i * hunk_size;

            if (compression_type == 2) {
                if (i > 0) {
                    std::memcpy(&disc_data_[dst_offset],
                                &disc_data_[(i - 1) * hunk_size],
                                hunk_size);
                } else {
                    std::memset(&disc_data_[dst_offset], 0, hunk_size);
                }
            } else if (compression_type == 1) {
                u64 next_off = static_cast<u64>(f.tellg());
                if (i + 1 < total_hunks) {
                    std::memcpy(&next_off, &map[(i + 1) * 12], 8);
                    next_off &= 0x3FFFFFFFFFFFFFFFULL;
                } else {
                    f.seekg(0, std::ios::end);
                    next_off = static_cast<u64>(f.tellg());
                }
                u32 comp_size = static_cast<u32>(next_off - block_offset);
                if (comp_size > hunk_size * 4) comp_size = hunk_size * 2;
                f.seekg(static_cast<i64>(block_offset));
                compressed.resize(comp_size);
                f.read(reinterpret_cast<char*>(compressed.data()), comp_size);
#ifdef YAPS1_HAS_ZLIB
                uLongf dst_len = hunk_size;
                if (uncompress(&disc_data_[dst_offset], &dst_len,
                              compressed.data(), comp_size) != Z_OK) {
                    std::memset(&disc_data_[dst_offset], 0, hunk_size);
                }
#else
                LOG_ERROR("CHD requires zlib which is not available");
                return std::unexpected(Error{"CHD support requires zlib"});
#endif
            } else {
                f.seekg(static_cast<i64>(block_offset));
                f.read(reinterpret_cast<char*>(&disc_data_[dst_offset]), hunk_size);
            }
        }
    } else {
        f.seekg(0, std::ios::end);
        auto size = static_cast<u32>(f.tellg());
        f.seekg(0);
        disc_data_.resize(size);
        f.read(reinterpret_cast<char*>(disc_data_.data()), size);
        disc_size_ = size;
    }

    if (disc_size_ % 2352 == 0) {
        sector_size_ = 2352;
    } else if (disc_size_ % 2048 == 0) {
        sector_size_ = 2048;
    } else {
        sector_size_ = 2352;
    }

    LOG_INFO("CHD loaded: {} ({} bytes)", path.filename().string(), disc_size_);
    return true;
}

// ===========================================================================
//  Timing
// ===========================================================================

void Cdrom::tick(u32 cycles) {
    cycle_acc_ += cycles;
    while (cycle_acc_ >= CYCLES_PER_SECTOR) {
        cycle_acc_ -= CYCLES_PER_SECTOR;
        if (reading_ && data_count_ == 0 && sector_count_ > 0) {
            read_sector(current_sector_);
            sector_count_--;
        }
    }
}

} // namespace yaps1
