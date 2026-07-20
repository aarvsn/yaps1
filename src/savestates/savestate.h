#pragma once

/// @file savestate.h
/// @brief Save state management — serialize/deserialize full system state.

#include "common/types.h"
#include <vector>
#include <string>

namespace yaps1 {

class System;

/// Manages save state slots. Each slot stores a compressed snapshot of the
/// entire emulator state (CPU registers, RAM, VRAM, SPU RAM, etc.).
class SaveStateManager {
public:
    SaveStateManager() = default;
    ~SaveStateManager() = default;

    /// Set the directory where save states are stored.
    void set_directory(const Path& dir) { save_dir_ = dir; }

    /// Save the current system state to the given slot.
    Result<bool> save(System& sys, int slot);

    /// Load a system state from the given slot.
    Result<bool> load(System& sys, int slot);

    /// Check if a save state exists for the given slot.
    [[nodiscard]] bool exists(int slot) const;

    /// List all available save state slots.
    [[nodiscard]] std::vector<int> list_slots() const;

private:
    Path save_dir_;

    /// Build the file path for a slot.
    [[nodiscard]] Path slot_path(int slot) const;

    /// Serialize the system state to a byte buffer.
    ByteVec serialize(System& sys);

    /// Deserialize a byte buffer into the system state.
    Result<bool> deserialize(System& sys, const ByteVec& data);
};

} // namespace yaps1