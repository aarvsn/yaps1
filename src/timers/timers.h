#pragma once

/// @file timers.h
/// @brief PS1 root counters (timers 0, 1, 2).

#include "common/types.h"
#include <array>

namespace yaps1 {

/// PS1 has 3 root counters (timers) at 0x1F801100.
/// Each can count system clock, dot clock, or hblank.
class Timers {
public:
    static constexpr int NUM_TIMERS = 3;

    Timers() = default;
    ~Timers() = default;

    void reset();

    /// Read/write timer registers (offset from 0x1F801100).
    void write16(u32 offset, u16 val);
    void write32(u32 offset, u32 val);
    [[nodiscard]] u16 read16(u32 offset);
    [[nodiscard]] u32 read32(u32 offset);

    /// Advance all timers by the given number of system cycles.
    void tick(u32 cycles);

    /// Check if any timer IRQ is pending.
    [[nodiscard]] bool irq_pending(int timer) const;
    void clear_irq(int timer);
    u32 irq_bits() const;

    /// External signals from GPU.
    void signal_hblank(bool active);
    void signal_vblank(bool active);

private:
    struct Timer {
        u16 value = 0;       // Current counter value
        u16 mode = 0;        // Mode register
        u16 target = 0;      // Target value
        u32 counter = 0;     // Internal cycle accumulator
        bool irq_pending = false;
        bool reached_target = false;
        bool reached_overflow = false;
        bool paused = false;
    };

    std::array<Timer, NUM_TIMERS> timers_{};

    /// Clock sources: 0=system, 1=dot_clock, 2=hblank, 3=external (gate)
    u32 get_clock_divider(int timer) const;

    /// External state.
    bool hblank_active_ = false;
    bool vblank_active_ = false;
    u32  dot_clock_acc_ = 0;
};

} // namespace yaps1