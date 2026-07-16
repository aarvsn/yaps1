/// @file system.cpp
/// @brief System implementation — wires all subsystems together.

#include "frontend/system.h"
#include "common/logger.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <algorithm>

namespace yaps1 {

// ===========================================================================
//  Constructor
// ===========================================================================

System::System()
    : cpu_(&bus_)
{
    // The CPU holds a pointer to the bus.  The bus's subsystem pointers
    // are set in init().
}

System::~System() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

// ===========================================================================
//  Initialize
// ===========================================================================

Result<bool> System::init(const Config& config, const Path& exe_dir) {
    config_ = config;

    // Set up directories.
    bios_dir_      = exe_dir / "bios";
    memcard_dir_   = exe_dir / "memcards";
    savestate_dir_ = exe_dir / "savestates";

    std::filesystem::create_directories(memcard_dir_);
    std::filesystem::create_directories(savestate_dir_);
    savestate_mgr_.set_directory(savestate_dir_);

    // ---- Set log level ----
    LogLevel level = LogLevel::Info;
    if (config.log_level == "debug")       level = LogLevel::Debug;
    else if (config.log_level == "warning") level = LogLevel::Warning;
    else if (config.log_level == "error")   level = LogLevel::Error;
    Logger::instance().set_level(level);

    // ---- Load BIOS ----
    Path bios_path;
    if (config.bios != "auto") {
        bios_path = bios_dir_ / config.bios;
    } else {
        // Scan for BIOS files, prefer SCPH1001.BIN.
        std::vector<Path> bios_files;
        Path preferred;

        if (std::filesystem::exists(bios_dir_)) {
            for (const auto& entry : std::filesystem::directory_iterator(bios_dir_)) {
                if (!entry.is_regular_file()) continue;
                auto name = entry.path().filename().string();
                // Convert to uppercase for comparison.
                std::string upper = name;
                std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (upper.ends_with(".BIN")) {
                    bios_files.push_back(entry.path());
                    if (upper == "SCPH1001.BIN") preferred = entry.path();
                }
            }
        }

        if (preferred.empty() && !bios_files.empty()) {
            preferred = bios_files[0];
        }

        if (preferred.empty()) {
            return std::unexpected(Error{
                "No PlayStation BIOS found.\n"
                "Place a BIOS inside:\n  bios/\n"
                "Recommended: SCPH1001.BIN"});
        }
        bios_path = preferred;
    }

    auto bios_result = bus_.load_bios(bios_path);
    if (!bios_result) return std::unexpected(bios_result.error());

    // ---- Wire subsystems to the bus ----
    bus_.set_cpu(&cpu_);
    bus_.set_gpu(&gpu_);
    bus_.set_spu(&spu_);
    bus_.set_cdrom(&cdrom_);
    bus_.set_dma(&dma_);
    bus_.set_timers(&timers_);
    bus_.set_controller(&pad_);

    // ---- Wire subsystems to each other ----
    gpu_.set_bus(&bus_);
    spu_.set_bus(&bus_);
    cdrom_.set_bus(&bus_);
    dma_.set_bus(&bus_);
    dma_.set_gpu(&gpu_);
    dma_.set_spu(&spu_);
    dma_.set_cdrom(&cdrom_);
    pad_.set_memcard(&memcards_[0]);

    // ---- Initialize subsystems ----
    cpu_.reset();
    gpu_.reset();
    spu_.reset();
    cdrom_.reset();
    dma_.reset();
    timers_.reset();
    pad_.reset();

    // ---- Load memory cards ----
    for (int i = 0; i < 2; i++) {
        Path card_path = memcard_dir_ / ("card" + std::to_string(i + 1) + ".mcd");
        auto result = memcards_[i].load(card_path, i + 1);
        if (!result) {
            LOG_WARN("Memory card {} issue: {}", i + 1, result.error().message);
        }
    }

    // ---- Initialize SDL for input ----
    pad_.init_sdl();

    // ---- Create Window and Renderer for GPU display ----
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        window_ = SDL_CreateWindow("YAPS1 — Yet Another PlayStation 1 Emulator", 1024, 512, 0);
        if (window_) {
            renderer_ = SDL_CreateRenderer(window_, nullptr);
            if (renderer_) {
                texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB1555, SDL_TEXTUREACCESS_STREAMING, 1024, 512);
                if (texture_) {
                    LOG_INFO("Created SDL window & renderer successfully");
                } else {
                    LOG_WARN("Failed to create SDL texture: {}", SDL_GetError());
                }
            } else {
                LOG_WARN("Failed to create SDL renderer: {}", SDL_GetError());
            }
        } else {
            LOG_WARN("Failed to create SDL window: {}", SDL_GetError());
        }
    }

    LOG_INFO("YAPS1 initialized successfully");
    return true;
}

// ===========================================================================
//  Load disc
// ===========================================================================

Result<bool> System::load_disc(const Path& path) {
    return cdrom_.load_disc(path);
}

// ===========================================================================
//  Frame
// ===========================================================================

void System::frame() {
    // Run the CPU for approximately one frame's worth of cycles.
    u32 cycles_remaining = CYCLES_PER_FRAME;

    while (cycles_remaining > 0) {
        u32 executed = cpu_.step();
        cycles_remaining -= std::min(executed, cycles_remaining);

        // Tick subsystems.
        timers_.tick(executed);
        gpu_.tick(executed);
        cdrom_.tick(executed);
        spu_.tick(executed);
        dma_.tick(executed);

        // Check for subsystem IRQs and forward to CPU.
        u32 irq = 0;
        if (gpu_.irq_pending())     irq |= (1u << 0);
        if (cdrom_.irq_pending())   irq |= (1u << 2);
        if (dma_.irq_active())      irq |= (1u << 3);
        irq |= timers_.irq_bits() << 4;  // Timers 0-2 at bits 4-6

        if (irq) {
            cpu_.trigger_irq(irq);
        }

        // Clear subsystem IRQs after signaling CPU.
        gpu_.clear_irq();
        // CD-ROM and timer IRQs are cleared by the game via register writes.
    }

    // Poll input.
    pad_.update();

    // ---- Render GPU frame to window ----
    if (renderer_ && texture_) {
        SDL_UpdateTexture(texture_, nullptr, gpu_.vram(), 1024 * sizeof(u16));
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    // Track FPS.
    update_fps();
}

// ===========================================================================
//  Reset
// ===========================================================================

void System::reset() {
    cpu_.reset();
    gpu_.reset();
    spu_.reset();
    cdrom_.reset();
    dma_.reset();
    timers_.reset();
    pad_.reset();
    LOG_INFO("System reset");
}

// ===========================================================================
//  Save memory cards
// ===========================================================================

void System::save_memcards() {
    for (int i = 0; i < 2; i++) {
        auto result = memcards_[i].save();
        if (!result) {
            LOG_WARN("Failed to save memory card {}: {}", i + 1, result.error().message);
        }
    }
}

// ===========================================================================
//  FPS
// ===========================================================================

void System::update_fps() {
    frame_count_++;
    auto now = std::chrono::steady_clock::now();
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                      now.time_since_epoch()).count();

    if (last_time_us_ == 0) {
        last_time_us_ = now_us;
        return;
    }

    fps_acc_ += 1.0;
    double elapsed_sec = static_cast<double>(now_us - last_time_us_) / 1000000.0;
    if (elapsed_sec >= 1.0) {
        current_fps_ = static_cast<u32>(fps_acc_ / elapsed_sec);
        fps_acc_ = 0.0;
        last_time_us_ = now_us;
    }
}

} // namespace yaps1