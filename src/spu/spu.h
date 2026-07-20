#pragma once

/// @file spu.h
/// @brief PS1 SPU (Sound Processing Unit) — ADPCM, reverb, CD audio.

#include "common/types.h"
#include <array>
#include <vector>
#include <mutex>
#include <atomic>

namespace yaps1 {

class BusInterface;

/// The PlayStation 1 SPU handles all audio: ADPCM voices, CD-DA, reverb.
class Spu {
public:
    Spu();
    ~Spu() = default;

    void reset();
    void set_bus(BusInterface* bus) { bus_ = bus; }

    // ---- Register interface ----
    void  write8(u32 offset, u8 val);
    void  write16(u32 offset, u16 val);
    void  write32(u32 offset, u32 val);
    [[nodiscard]] u8   read8(u32 offset);
    [[nodiscard]] u16  read16(u32 offset);
    [[nodiscard]] u32  read32(u32 offset);

    // ---- DMA interface ----
    void dma_write(const u8* data, u32 bytes);

    // ---- Raw access (for save-states) ----
    [[nodiscard]] u8* ram() { return ram_.data(); }

    // ---- Audio output ----
    /// Mix all active voices and produce output samples.
    /// stereo_out: pointer to interleaved stereo sample buffer (left, right, left, right, ...)
    /// count: number of stereo frames (pairs) to produce.
    void mix(i16* stereo_out, u32 count);

    // ---- Timing ----
    void tick(u32 cycles);

    // ---- Interrupt ----
    [[nodiscard]] bool irq_pending() const { return irq_pending_; }
    void clear_irq() { irq_pending_ = false; }

private:
    static constexpr u32 NUM_VOICES = 24;
    static constexpr u32 SPU_RAM_SIZE = 512 * 1024;  // 512 KB

    // ---- SPU RAM ----
    std::array<u8, SPU_RAM_SIZE> ram_{};

    // ---- Voice state ----
    struct Voice {
        u16 volume_left  = 0;   // Volume L (0x000-0x3FFF)
        u16 volume_right = 0;   // Volume R
        u16 pitch        = 0;   // Sample rate (pitch)
        u32 start_addr   = 0;   // Start address in SPU RAM
        u32 adsr_low     = 0;   // ADSR low word
        u32 adsr_high    = 0;   // ADSR high word
        u32 adsr_vol     = 0;   // Current ADSR volume (12-bit)
        bool on          = false;
        bool mode        = false; // ADPCM mode (1 = repeat)
        i16  prev_sample = 0;   // ADPCM decoder state
        i16  prev_step   = 0;
        u32  current_addr = 0;
        u32  loop_addr    = 0;
        u32  adsr_phase   = 0;  // 0=attack,1=decay,2=sustain,3=release
        u32  adsr_counter = 0;
    };

    std::array<Voice, NUM_VOICES> voices_{};

    // ---- Main volume ----
    u16 main_vol_left_  = 0;
    u16 main_vol_right_ = 0;

    // ---- Reverb ----
    bool reverb_on_ = false;
    std::array<i16, 0x40000> reverb_work_{};
    u32 reverb_base_ = 0;

    // ---- CD Audio ----
    bool cd_audio_on_ = false;
    i16  cd_audio_left_ = 0;
    i16  cd_audio_right_ = 0;

    // ---- Control register (0x1F801DA8) ----
    u16 control_ = 0;

    // ---- Status register (0x1F801DAA) ----
    u16 status_ = 0;

    // ---- IRQ ----
    u32 irq_addr_ = 0;
    bool irq_pending_ = false;

    // ---- Transfer ----
    bool transfer_busy_ = false;
    u16  transfer_addr_ = 0;

    // ---- ADSR envelope step ----
    void adsr_step(Voice& v);

    // ---- ADPCM decode ----
    /// Decode a single ADPCM nibble.
    i16 adpcm_decode(Voice& v, u8 nibble);

    BusInterface* bus_ = nullptr;
};

} // namespace yaps1