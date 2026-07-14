/// @file spu.cpp
/// @brief SPU implementation.

#include "spu/spu.h"
#include "common/logger.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace yaps1 {

// ADPCM decoding table (standard IMA-like, but PS1 uses its own variant).
static constexpr i16 ADPCM_TABLE[5][2][4] = {
    { {  0,  0,  0,  0 }, {  0,  0,  0,  0 } },  // Step 0
    { {  0, -60, 0, -60 }, {  0,  60, 0,  60 } },  // Step 1
    { { -115, -52, -115, -38 }, { 115, 52, 115, 38 } },  // Step 2
    { { -98, -198, -98, -326 }, { 98, 198, 98, 326 } },  // Step 3
    { { -122, -60, -122, -34 }, { 122, 60, 122, 34 } },  // Step 4
};

Spu::Spu() { reset(); }

void Spu::reset() {
    ram_.fill(0);
    for (auto& v : voices_) {
        v = Voice{};
    }
    main_vol_left_ = 0;
    main_vol_right_ = 0;
    reverb_on_ = false;
    reverb_work_.fill(0);
    cd_audio_on_ = false;
    control_ = 0;
    status_ = 0;
    irq_addr_ = 0;
    irq_pending_ = false;
    transfer_busy_ = false;
    transfer_addr_ = 0;
}

// ===========================================================================
//  Register read/write
// ===========================================================================

void Spu::write16(u32 offset, u16 val) {
    // SPU register map (offsets from 0x1F801C00):
    //   0x000-0x1DF  Voice registers (16 bytes each: volL, volR, pitch, start, ADSR)
    //   0x1E0        Sound On/Off key
    //   0x1E2        Sound On/Off key (second write)
    //   0x1E4        Channel FM mode
    //   0x1E6        Channel noise mode
    //   0x1E8        Channel reverb mode
    //   0x1EA        Channel On/Off status
    //   0x1EC        Sound RAM data transfer control
    //   0x1EE        Sound RAM data transfer FIFO
    //   0x1F0-0x1F7  SPU voices 0-15 ON/OFF
    //   0x1F8-0x1FF  SPU voices 16-23 ON/OFF + CD audio
    //   0x200-0x25F  Reverb registers
    //   0x260        Reverb work area start
    //   0x2A8        Main volume L
    //   0x2AA        Main volume R
    //   0x2AC        SPU control
    //   0x2AE        SPU status
    //   0x2B0        CD audio volume L
    //   0x2B2        CD audio volume R

    // Voice key on
    if (offset == 0x1E0) {
        for (int i = 0; i < 16; i++) {
            if (val & (1u << i)) {
                voices_[i].on = true;
                voices_[i].current_addr = voices_[i].start_addr;
                voices_[i].adsr_vol = 0;
                voices_[i].adsr_phase = 0;  // Attack
                voices_[i].adsr_counter = 0;
            }
        }
        return;
    }
    if (offset == 0x1E2) {
        for (int i = 0; i < 8; i++) {
            if (val & (1u << i)) {
                voices_[16 + i].on = true;
                voices_[16 + i].current_addr = voices_[16 + i].start_addr;
                voices_[16 + i].adsr_vol = 0;
                voices_[16 + i].adsr_phase = 0;
                voices_[16 + i].adsr_counter = 0;
            }
        }
        return;
    }

    // Key off
    if (offset == 0x1E4) {
        for (int i = 0; i < 16; i++) {
            if (val & (1u << i)) {
                voices_[i].adsr_phase = 3;  // Release
            }
        }
        return;
    }
    if (offset == 0x1E6) {
        for (int i = 0; i < 8; i++) {
            if (val & (1u << i)) {
                voices_[16 + i].adsr_phase = 3;
            }
        }
        return;
    }

    // Voice registers (each voice: 8 x 16-bit regs = 16 bytes)
    if (offset < 0x180) {
        int voice_idx = offset / 16;
        int reg = (offset % 16) / 2;
        if (voice_idx < NUM_VOICES) {
            switch (reg) {
                case 0: voices_[voice_idx].volume_left = val;  break;
                case 1: voices_[voice_idx].volume_right = val; break;
                case 2: voices_[voice_idx].pitch = val;        break;
                case 3: voices_[voice_idx].start_addr = val & 0x7FFFF; break;
                case 4: voices_[voice_idx].adsr_low = val;     break;
                case 5: voices_[voice_idx].adsr_high = val;    break;
                case 6:
                    voices_[voice_idx].loop_addr = val & 0x7FFFF;
                    break;
            }
        }
        return;
    }

    // Transfer control
    if (offset == 0x1EC) {
        transfer_busy_ = (val & 0x80) != 0;
        if (val & 0x10) transfer_addr_ = 0;  // Reset transfer address
        return;
    }

    // Transfer FIFO
    if (offset == 0x1EE) {
        // Write a byte to SPU RAM at the transfer address.
        // Each 16-bit write = 1 byte in SPU RAM (only lower byte used).
        u32 addr = (transfer_addr_ * 8) & (SPU_RAM_SIZE - 1);
        ram_[addr] = static_cast<u8>(val & 0xFF);
        transfer_addr_ = (transfer_addr_ + 1) & 0xFFFF;
        return;
    }

    // Main volume
    if (offset == 0x2A8) { main_vol_left_ = val; return; }
    if (offset == 0x2AA) { main_vol_right_ = val; return; }

    // Control
    if (offset == 0x2AC) {
        control_ = val;
        // Bit 15: enable/disable SPU
        // Bit 14: mute
        // Bit 7: external audio reverb (CD audio)
        // Bit 6: external audio enable (CD audio)
        cd_audio_on_ = (val & 0x40) != 0;
        return;
    }

    // IRQ address
    if (offset == 0x2A6) {
        irq_addr_ = val * 8;
        return;
    }

    // Reverb on/off per channel
    if (offset == 0x1E8) {
        for (int i = 0; i < 16; i++) {
            // val bit i = reverb on for voice i
            (void)i;
        }
        return;
    }

    // CD audio volume
    if (offset == 0x2B0 || offset == 0x2B2) {
        // CD audio volume L/R — store for mixing.
        return;
    }
}

void Spu::write8(u32 offset, u8 val) {
    write16(offset & ~1u, static_cast<u16>(val));
}

void Spu::write32(u32 offset, u32 val) {
    write16(offset,      static_cast<u16>(val & 0xFFFF));
    write16(offset + 2,  static_cast<u16>((val >> 16) & 0xFFFF));
}

u16 Spu::read16(u32 offset) {
    // Voice ON/OFF status
    if (offset == 0x1EA) {
        u16 result = 0;
        for (int i = 0; i < NUM_VOICES; i++) {
            if (voices_[i].on) result |= (1u << i);
        }
        return result;
    }

    // Status
    if (offset == 0x2AE) {
        status_ = 0;
        if (irq_pending_) status_ |= (1u << 11);
        if (transfer_busy_) status_ |= (1u << 10);
        // Second half of SPU RAM DMA
        // Bit 9: SPU DMA
        status_ |= (1u << 9);
        return status_;
    }

    // Main volume
    if (offset == 0x2A8) return main_vol_left_;
    if (offset == 0x2AA) return main_vol_right_;

    // Transfer FIFO
    if (offset == 0x1EE) {
        u32 addr = (transfer_addr_ * 8) & (SPU_RAM_SIZE - 1);
        u8 val = ram_[addr];
        transfer_addr_ = (transfer_addr_ + 1) & 0xFFFF;
        return static_cast<u16>(val);
    }

    return 0;
}

u8 Spu::read8(u32 offset) {
    return static_cast<u8>(read16(offset & ~1u));
}

u32 Spu::read32(u32 offset) {
    u16 lo = read16(offset);
    u16 hi = read16(offset + 2);
    return (static_cast<u32>(hi) << 16) | lo;
}

// ===========================================================================
//  DMA
// ===========================================================================

void Spu::dma_write(const u8* data, u32 bytes) {
    u32 addr = (transfer_addr_ * 8) & (SPU_RAM_SIZE - 1);
    for (u32 i = 0; i < bytes; i++) {
        ram_[addr] = data[i];
        addr = (addr + 1) & (SPU_RAM_SIZE - 1);
    }
    transfer_addr_ = (transfer_addr_ + bytes / 4) & 0xFFFF;
}

// ===========================================================================
//  ADSR envelope
// ===========================================================================

void Spu::adsr_step(Voice& v) {
    // Simplified ADSR — real PS1 ADSR uses log-scaled tables.
    // This is functional enough for basic audio playback.

    u32 sustain_level = (v.adsr_high >> 4) & 0xF;
    u32 sustain_mode  = (v.adsr_high >> 8) & 7;
    u32 attack_rate   = (v.adsr_low >> 0) & 0x7F;
    u32 decay_rate    = (v.adsr_low >> 8) & 0xF;
    u32 release_rate  = (v.adsr_low >> 12) & 0xF;

    switch (v.adsr_phase) {
        case 0: { // Attack
            v.adsr_counter++;
            u32 rate = attack_rate >> 2;
            if (rate == 0) rate = 1;
            if (v.adsr_counter >= (4 * rate)) {
                v.adsr_counter = 0;
                v.adsr_vol += 16;
                if (v.adsr_vol >= 0x7FFF) {
                    v.adsr_vol = 0x7FFF;
                    v.adsr_phase = 1;  // Decay
                }
            }
            break;
        }
        case 1: { // Decay
            v.adsr_counter++;
            u32 rate = (decay_rate >> 2) + 1;
            if (v.adsr_counter >= (4 * rate * 8)) {
                v.adsr_counter = 0;
                v.adsr_vol -= 8;
                u32 sus_level = sustain_level * 0x800;
                if (v.adsr_vol <= sus_level) {
                    v.adsr_vol = sus_level;
                    v.adsr_phase = 2;  // Sustain
                }
            }
            break;
        }
        case 2: { // Sustain
            if (sustain_mode == 0) {
                // Infinite sustain
            } else {
                v.adsr_counter++;
                u32 rate = (sustain_mode >> 2) + 1;
                if (v.adsr_counter >= (4 * rate * 4)) {
                    v.adsr_counter = 0;
                    if (sustain_mode & 1) {
                        v.adsr_vol = std::max(0u, v.adsr_vol - 8);
                    } else {
                        v.adsr_vol += 8;
                        if (v.adsr_vol > 0x7FFF) v.adsr_vol = 0x7FFF;
                    }
                }
            }
            break;
        }
        case 3: { // Release
            v.adsr_counter++;
            u32 rate = (release_rate >> 2) + 1;
            if (v.adsr_counter >= (4 * rate * 4)) {
                v.adsr_counter = 0;
                v.adsr_vol -= 8;
                if (v.adsr_vol <= 0) {
                    v.adsr_vol = 0;
                    v.on = false;
                }
            }
            break;
        }
    }
}

// ===========================================================================
//  ADPCM decode
// ===========================================================================

i16 Spu::adpcm_decode(Voice& v, u8 nibble) {
    // PS1-style ADPCM decoding.
    // The step size is derived from the pitch register.
    i32 step = v.pitch;

    i32 diff = static_cast<i32>(nibble) - 8;  // Center around 0
    if (diff >= 0) diff++;  // Make it -7..+8

    // Simple filter: mix previous sample with predicted sample.
    i32 sample = v.prev_sample + (diff * step / 8);

    // Clamp to 16-bit
    sample = std::clamp(sample, -32768, 32767);

    v.prev_sample = static_cast<i16>(sample);
    return static_cast<i16>(sample);
}

// ===========================================================================
//  Mix — produce audio output
// ===========================================================================

void Spu::mix(i16* stereo_out, u32 count) {
    // Main volume (simplified: 0x0000 = silent, 0x3FFF = max)
    i32 vol_l = main_vol_left_;
    i32 vol_r = main_vol_right_;

    for (u32 frame = 0; frame < count; frame++) {
        i32 mix_l = 0;
        i32 mix_r = 0;

        // Mix each active voice
        for (int i = 0; i < NUM_VOICES; i++) {
            Voice& v = voices_[i];
            if (!v.on) continue;

            // Advance ADSR
            adsr_step(v);

            // Read ADPCM data from SPU RAM
            u32 addr = (v.current_addr * 8) & (SPU_RAM_SIZE - 1);
            u8 byte0 = ram_[addr];
            // u8 byte1 = ram_[addr + 1];

            // Each byte contains 2 nibbles = 2 samples.
            // For simplicity, decode one sample per voice per frame.
            i16 sample = adpcm_decode(v, byte0 >> 4);

            // Apply voice volume (simplified linear scaling)
            i32 voice_l = (sample * v.volume_left) / 0x4000;
            i32 voice_r = (sample * v.volume_right) / 0x4000;

            // Apply ADSR volume
            voice_l = (voice_l * v.adsr_vol) / 0x7FFF;
            voice_r = (voice_r * v.adsr_vol) / 0x7FFF;

            mix_l += voice_l;
            mix_r += voice_r;

            // Advance address
            v.current_addr = (v.current_addr + 1) & 0x7FFFF;

            // Check IRQ
            if ((v.current_addr * 8) == irq_addr_) {
                irq_pending_ = true;
            }
        }

        // Apply main volume
        mix_l = (mix_l * vol_l) / 0x3FFF;
        mix_r = (mix_r * vol_r) / 0x3FFF;

        // Mix CD audio
        if (cd_audio_on_) {
            mix_l += cd_audio_left_;
            mix_r += cd_audio_right_;
        }

        // Clamp and output
        mix_l = std::clamp(mix_l, -32768, 32767);
        mix_r = std::clamp(mix_r, -32768, 32767);

        stereo_out[frame * 2]     = static_cast<i16>(mix_l);
        stereo_out[frame * 2 + 1] = static_cast<i16>(mix_r);
    }
}

// ===========================================================================
//  Timing
// ===========================================================================

void Spu::tick(u32 cycles) {
    // SPU runs at the same clock as the CPU (33.8688 MHz).
    // Audio is mixed at 44100 Hz.  We don't need to do much here
    // since mix() is called at the right rate by the frontend.
    (void)cycles;
}

} // namespace yaps1