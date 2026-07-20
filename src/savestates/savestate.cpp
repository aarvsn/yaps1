/// @file savestate.cpp
/// @brief Save state implementation.

#include "savestates/savestate.h"
#include "common/types.h"
#include "common/logger.h"
#include <fstream>

#ifdef YAPS1_HAS_ZLIB
#include <zlib.h>
#endif

// Forward declaration of System (defined in frontend/system.h).
#include "frontend/system.h"

namespace yaps1 {

// ===========================================================================
//  Slot path
// ===========================================================================

Path SaveStateManager::slot_path(int slot) const {
    return save_dir_ / ("slot" + std::to_string(slot) + ".yss");
}

bool SaveStateManager::exists(int slot) const {
    return std::filesystem::exists(slot_path(slot));
}

std::vector<int> SaveStateManager::list_slots() const {
    std::vector<int> slots;
    if (!std::filesystem::exists(save_dir_)) return slots;
    for (int i = 0; i < 10; i++) {
        if (exists(i)) slots.push_back(i);
    }
    return slots;
}

// ===========================================================================
//  Serialize
// ===========================================================================

ByteVec SaveStateManager::serialize(System& sys) {
    ByteVec buf;
    auto write_u32 = [&](u32 val) {
        buf.push_back(val & 0xFF);
        buf.push_back((val >> 8) & 0xFF);
        buf.push_back((val >> 16) & 0xFF);
        buf.push_back((val >> 24) & 0xFF);
    };
    auto write_u64 = [&](u64 val) {
        for (int i = 0; i < 8; i++) buf.push_back((val >> (i * 8)) & 0xFF);
    };
    auto write_data = [&](const void* data, size_t len) {
        auto* ptr = static_cast<const u8*>(data);
        buf.insert(buf.end(), ptr, ptr + len);
    };

    write_data("YSS1", 4);
    write_u32(1);

    auto& cpu = sys.cpu();
    write_u32(cpu.pc());
    write_u32(cpu.hi());
    write_u32(cpu.lo());
    for (int i = 0; i < 32; i++) write_u32(cpu.gpr(i));
    write_u64(cpu.cycles());

    write_data(sys.ram(), 2 * 1024 * 1024);
    write_data(sys.scratchpad(), 1024);
    write_data(sys.gpu_vram(), 1024 * 512 * 2);
    write_data(sys.spu_ram(), 512 * 1024);

    return buf;
}

// ===========================================================================
//  Save
// ===========================================================================

Result<bool> SaveStateManager::save(System& sys, int slot) {
    if (!std::filesystem::exists(save_dir_)) {
        std::filesystem::create_directories(save_dir_);
    }

    ByteVec raw = serialize(sys);

#ifdef YAPS1_HAS_ZLIB
    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<u8> compressed(compressed_size);
    int ret = compress2(compressed.data(), &compressed_size,
                        raw.data(), static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
        Error e{"Failed to compress save state"};
        return std::unexpected(std::move(e));
    }

    Path path = slot_path(slot);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        Error e{"Cannot write save state: " + path.string()};
        return std::unexpected(std::move(e));
    }

    u32 raw_size = static_cast<u32>(raw.size());
    f.write(reinterpret_cast<const char*>(&raw_size), 4);
    f.write(reinterpret_cast<const char*>(compressed.data()), compressed_size);
    if (!f) {
        Error e{"Write error: " + path.string()};
        return std::unexpected(std::move(e));
    }
#else
    Path path = slot_path(slot);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        Error e{"Cannot write save state: " + path.string()};
        return std::unexpected(std::move(e));
    }
    u32 raw_size = static_cast<u32>(raw.size());
    f.write(reinterpret_cast<const char*>(&raw_size), 4);
    f.write(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (!f) {
        Error e{"Write error: " + path.string()};
        return std::unexpected(std::move(e));
    }
#endif

    LOG_INFO("Save state saved to slot {}", slot);
    return true;
}

// ===========================================================================
//  Load
// ===========================================================================

Result<bool> SaveStateManager::load(System& sys, int slot) {
    Path path = slot_path(slot);
    if (!std::filesystem::exists(path)) {
        Error e{"No save state at slot " + std::to_string(slot)};
        return std::unexpected(std::move(e));
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        Error e{"Cannot read save state: " + path.string()};
        return std::unexpected(std::move(e));
    }
    auto file_size = f.tellg();
    f.seekg(0);

    std::vector<u8> file_data(static_cast<size_t>(file_size));
    f.read(reinterpret_cast<char*>(file_data.data()), file_size);
    if (!f) {
        Error e{"Read error: " + path.string()};
        return std::unexpected(std::move(e));
    }

    u32 raw_size;
    std::memcpy(&raw_size, file_data.data(), 4);

#ifdef YAPS1_HAS_ZLIB
    std::vector<u8> compressed(file_data.begin() + 4, file_data.end());
    ByteVec raw(raw_size);
    uLongf dest_len = raw_size;
    int ret = uncompress(raw.data(), &dest_len, compressed.data(), static_cast<uLong>(compressed.size()));
    if (ret != Z_OK || dest_len != raw_size) {
        Error e{"Failed to decompress save state"};
        return std::unexpected(std::move(e));
    }
#else
    ByteVec raw(file_data.begin() + 4, file_data.end());
#endif

    return deserialize(sys, raw);
}

// ===========================================================================
//  Deserialize
// ===========================================================================

Result<bool> SaveStateManager::deserialize(System& sys, const ByteVec& data) {
    size_t pos = 0;
    auto read_u32 = [&](u32& val) -> bool {
        if (pos + 4 > data.size()) return false;
        val = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);
        pos += 4;
        return true;
    };
    auto read_u64 = [&](u64& val) -> bool {
        if (pos + 8 > data.size()) return false;
        val = 0;
        for (int i = 0; i < 8; i++) val |= (static_cast<u64>(data[pos + i]) << (i * 8));
        pos += 8;
        return true;
    };
    auto read_data = [&](void* dst, size_t len) -> bool {
        if (pos + len > data.size()) return false;
        std::memcpy(dst, data.data() + pos, len);
        pos += len;
        return true;
    };

    if (data.size() < 8) {
        Error e{"Save state too small"};
        return std::unexpected(std::move(e));
    }
    if (std::memcmp(data.data(), "YSS1", 4) != 0) {
        Error e{"Invalid save state magic"};
        return std::unexpected(std::move(e));
    }
    pos = 4;
    u32 version;
    if (!read_u32(version)) {
        Error e{"Truncated header"};
        return std::unexpected(std::move(e));
    }
    if (version != 1) {
        Error e{"Unsupported save state version"};
        return std::unexpected(std::move(e));
    }

    auto& cpu = sys.cpu();
    u32 pc, hi, lo; u64 cycles;
    if (!read_u32(pc)) {
        Error e{"Truncated CPU state"};
        return std::unexpected(std::move(e));
    }
    if (!read_u32(hi)) {
        Error e{"Truncated CPU state"};
        return std::unexpected(std::move(e));
    }
    if (!read_u32(lo)) {
        Error e{"Truncated CPU state"};
        return std::unexpected(std::move(e));
    }

    std::array<u32, 32> regs;
    for (int i = 0; i < 32; i++) {
        if (!read_u32(regs[i])) {
            Error e{"Truncated CPU registers"};
            return std::unexpected(std::move(e));
        }
    }
    if (!read_u64(cycles)) {
        Error e{"Truncated CPU cycles"};
        return std::unexpected(std::move(e));
    }

    cpu.set_pc(pc);
    cpu.set_hi(hi);
    cpu.set_lo(lo);
    for (int i = 1; i < 32; i++) cpu.set_reg(i, regs[i]);
    cpu.set_cycles(cycles);

    if (!read_data(sys.ram(), 2 * 1024 * 1024)) {
        Error e{"Truncated RAM"};
        return std::unexpected(std::move(e));
    }
    if (!read_data(sys.scratchpad(), 1024)) {
        Error e{"Truncated scratchpad"};
        return std::unexpected(std::move(e));
    }
    if (!read_data(sys.gpu_vram(), 1024 * 512 * 2)) {
        Error e{"Truncated VRAM"};
        return std::unexpected(std::move(e));
    }
    if (!read_data(sys.spu_ram(), 512 * 1024)) {
        Error e{"Truncated SPU RAM"};
        return std::unexpected(std::move(e));
    }

    LOG_INFO("Save state loaded successfully");
    return true;
}

} // namespace yaps1