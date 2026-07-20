/// @file timers.cpp
/// @brief PS1 root counters implementation.

#include "timers/timers.h"
#include "common/logger.h"

namespace yaps1 {

void Timers::reset() {
    for (auto& t : timers_) {
        t = Timer{};
    }
    hblank_active_ = false;
    vblank_active_ = false;
    dot_clock_acc_ = 0;
}

// ===========================================================================
//  Register layout: each timer = 0x10 bytes at 0x1F801100
//   +0x00: Current value (read-only, write resets to 0)
//   +0x04: Mode register
//   +0x08: Target value
// ===========================================================================

void Timers::write16(u32 offset, u16 val) {
    int timer_idx = static_cast<int>((offset / 0x10) & 3);
    int reg = (offset / 2) % 4;

    if (timer_idx >= NUM_TIMERS) return;
    auto& t = timers_[timer_idx];

    switch (reg) {
        case 0: // Counter value (write resets)
            t.value = 0;
            break;
        case 1: // Mode
            t.mode = val;
            // Bit 10: reset counter on target
            // Bit 11: reset counter on max
            // Bit 12: IRQ on target
            // Bit 13: IRQ on overflow
            // Bit 14: IRQ repeat mode
            // Bit 15: IRQ toggle/pulse mode
            break;
        case 2: // Target
            t.target = val;
            break;
    }
}

void Timers::write32(u32 offset, u32 val) {
    write16(offset,      static_cast<u16>(val & 0xFFFF));
    write16(offset + 2,  static_cast<u16>((val >> 16) & 0xFFFF));
}

u16 Timers::read16(u32 offset) {
    int timer_idx = static_cast<int>((offset / 0x10) & 3);
    int reg = (offset / 2) % 4;

    if (timer_idx >= NUM_TIMERS) return 0;
    auto& t = timers_[timer_idx];

    switch (reg) {
        case 0: return t.value;
        case 1: {
            u16 result = t.mode & 0x03FF;
            if (t.reached_target)  result |= (1u << 11);
            if (t.reached_overflow) result |= (1u << 12);
            return result;
        }
        case 2: return t.target;
    }
    return 0;
}

u32 Timers::read32(u32 offset) {
    u16 lo = read16(offset);
    u16 hi = read16(offset + 2);
    return (static_cast<u32>(hi) << 16) | lo;
}

// ===========================================================================
//  Clock dividers
// ===========================================================================

u32 Timers::get_clock_divider(int timer) const {
    u32 sync_mode = (timers_[timer].mode >> 2) & 3;
    switch (sync_mode) {
        case 0: return 1;           // System clock (33.87 MHz)
        case 1: return 8;           // Dot clock / 8 (~5.37 MHz)
        case 2: return 1;           // Hblank (count on each hblank)
        case 3: return 1;           // External (gate)
        default: return 1;
    }
}

// ===========================================================================
//  Tick
// ===========================================================================

void Timers::tick(u32 cycles) {
    for (int i = 0; i < NUM_TIMERS; i++) {
        auto& t = timers_[i];
        u32 sync_mode = (t.mode >> 2) & 3;

        // Check pause conditions.
        bool paused = false;
        if (sync_mode == 2) {
            // Hblank counter: only counts during hblank.
            paused = !hblank_active_;
        } else if (sync_mode == 3) {
            // Gate mode: bit 0 of mode = gate enable.
            paused = !(t.mode & 1);
        }

        if (paused) continue;

        u32 divider = get_clock_divider(i);
        t.counter += cycles;

        // How many counts have elapsed?
        u32 counts = t.counter / divider;
        t.counter %= divider;

        if (counts == 0) continue;

        u16 old_value = t.value;
        // Use 16-bit wrapping arithmetic.
        u32 new_val = static_cast<u32>(t.value) + counts;
        t.value = static_cast<u16>(new_val & 0xFFFF);

        // Check target.
        t.reached_target = false;
        if ((t.mode & (1u << 10))) {  // Reset on target
            if (old_value < t.target && t.value >= t.target) {
                t.reached_target = true;
                if (t.mode & (1u << 12)) {
                    t.irq_pending = true;
                }
            }
        }

        // Check overflow (0xFFFF -> 0x0000).
        t.reached_overflow = false;
        u16 prev = static_cast<u16>((static_cast<u32>(old_value) + counts) & 0xFFFF);
        if ((t.mode & (1u << 11))) {  // Reset on max
            if (prev < old_value) {  // Wrapped around
                t.reached_overflow = true;
                if (t.mode & (1u << 13)) {
                    t.irq_pending = true;
                }
            }
        }
    }
}

bool Timers::irq_pending(int timer) const {
    if (timer < NUM_TIMERS) return timers_[timer].irq_pending;
    return false;
}

void Timers::clear_irq(int timer) {
    if (timer < NUM_TIMERS) timers_[timer].irq_pending = false;
}

u32 Timers::irq_bits() const {
    u32 bits = 0;
    for (int i = 0; i < NUM_TIMERS; i++) {
        if (timers_[i].irq_pending) bits |= (1u << i);
    }
    return bits;
}

void Timers::signal_hblank(bool active) {
    hblank_active_ = active;
}

void Timers::signal_vblank(bool active) {
    vblank_active_ = active;
}

} // namespace yaps1