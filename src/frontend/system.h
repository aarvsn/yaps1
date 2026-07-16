#pragma once

/// @file system.h
/// @brief System — top-level emulator state that owns and connects all subsystems.

#include "common/types.h"
#include "common/bus.h"
#include "cpu/cpu.h"
#include "gpu/gpu.h"
#include "spu/spu.h"
#include "cdrom/cdrom.h"
#include "dma/dma.h"
#include "timers/timers.h"
#include "controllers/controller.h"
#include "memcards/memcard.h"
#include "savestates/savestate.h"
#include "config/config.h"
#include <memory>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace yaps1 {

/// Owns and orchestrates all PS1 hardware subsystems.
class System {
public:
    System();
    ~System();

    /// Full initialization: load BIOS, create directories, wire subsystems.
    Result<bool> init(const Config& config, const Path& exe_dir);

    /// Load a game disc.
    Result<bool> load_disc(const Path& path);

    /// Run the emulation for one frame (until VBlank).
    void frame();

    /// Reset the entire system.
    void reset();

    /// Save memory cards to disk.
    void save_memcards();

    // ---- Subsystem access (for save-states) ----
    [[nodiscard]] Cpu& cpu()             { return cpu_; }
    [[nodiscard]] Gpu& gpu()             { return gpu_; }
    [[nodiscard]] Spu& spu()             { return spu_; }
    [[nodiscard]] Cdrom& cdrom()         { return cdrom_; }
    [[nodiscard]] Dma& dma()             { return dma_; }
    [[nodiscard]] Timers& timers()       { return timers_; }
    [[nodiscard]] Pad& pad()             { return pad_; }
    [[nodiscard]] MemoryCard& memcard(int slot) { return memcards_[slot]; }
    [[nodiscard]] Bus& bus()             { return bus_; }

    // ---- Raw memory access (for save-states) ----
    [[nodiscard]] u8* ram()              { return bus_.ram(); }
    [[nodiscard]] u8* scratchpad()       { return bus_.scratchpad(); }
    [[nodiscard]] u16* gpu_vram()        { return gpu_.vram(); }
    [[nodiscard]] u8* spu_ram()          { return spu_.ram(); }

    // ---- Config ----
    [[nodiscard]] const Config& config() const { return config_; }

    // ---- Audio output ----
    /// Get the mixed audio buffer (stereo interleaved i16).
    void get_audio(i16* buf, u32 frames) { spu_.mix(buf, frames); }

    // ---- FPS tracking ----
    void update_fps();

private:
    Config config_;

    // Subsystems (order matters for construction).
    Bus     bus_;
    Cpu     cpu_;
    Gpu     gpu_;
    Spu     spu_;
    Cdrom   cdrom_;
    Dma     dma_;
    Timers  timers_;
    Pad     pad_;
    std::array<MemoryCard, 2> memcards_;

    SaveStateManager savestate_mgr_;

    // Paths
    Path bios_dir_;
    Path memcard_dir_;
    Path savestate_dir_;

    // FPS
    u32 frame_count_ = 0;
    u32 fps_timer_ = 0;
    u32 current_fps_ = 0;
    double fps_acc_ = 0.0;
    u64 last_time_us_ = 0;

    // Timing
    static constexpr u32 CPU_CLOCK_HZ = 33868800;
    static constexpr u32 CYCLES_PER_FRAME = CPU_CLOCK_HZ / 60;  // ~564480

    // SDL Window rendering members
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
};

} // namespace yaps1