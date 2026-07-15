/// @file main.cpp
/// @brief YAPS1 — Yet Another PlayStation 1 Emulator (terminal-based).

#include "frontend/system.h"
#include "common/logger.h"
#include "config/config.h"
#include "controllers/controller.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

using namespace yaps1;

static void signal_handler(int /*sig*/) {
    g_running.store(false);
}

/// Print the YAPS1 banner and startup sequence.
static void print_banner() {
    std::cerr << "\033[1;96m"
R"(  ____  _             ____  _
 |  _ \| | _____  __ |  _ \| | _____  __
 | |_) | |/ _ \ \/ / | |_) | |/ _ \ \/ /
 |  __/|   __/>  <  |  __/|   __/>  <
 |_|   |_|\___/_/\_\ |_|   |_|\___/_/\_\
)"
              << "\033[0m\n";
    std::cerr << "\033[1;97mYAPS1\033[0m v1.0.0\n\n";
}

/// Parse the mapping.toml file for keyboard configuration.
static void load_key_mapping(const Path& path, Pad& pad) {
    if (!std::filesystem::exists(path)) {
        // Generate default mapping.
        Config::generate_default_mapping(path);
        return;
    }

    std::ifstream f(path);
    if (!f.is_open()) return;

    // Minimal TOML parser for [player1] section.
    std::unordered_map<std::string, int> keys;
    std::string line;
    bool in_player1 = false;

    // Map PS1 button names to SDL scancodes.
    auto scancode_from_name = [](const std::string& name) -> int {
        auto upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        if (upper == "SPACE" || upper == "CROSS")     return SDL_SCANCODE_SPACE;
        if (upper == "L" || upper == "CIRCLE")         return SDL_SCANCODE_L;
        if (upper == "K" || upper == "SQUARE")         return SDL_SCANCODE_K;
        if (upper == "I" || upper == "TRIANGLE")       return SDL_SCANCODE_I;
        if (upper == "Q" || upper == "L1")             return SDL_SCANCODE_Q;
        if (upper == "E" || upper == "R1")             return SDL_SCANCODE_E;
        if (upper == "1" || upper == "L2")             return SDL_SCANCODE_1;
        if (upper == "3" || upper == "R2")             return SDL_SCANCODE_3;
        if (upper == "RIGHTSHIFT" || upper == "SELECT") return SDL_SCANCODE_RSHIFT;
        if (upper == "RETURN" || upper == "ENTER" || upper == "START") return SDL_SCANCODE_RETURN;
        if (upper == "W" || upper == "UP")             return SDL_SCANCODE_W;
        if (upper == "S" || upper == "DOWN")           return SDL_SCANCODE_S;
        if (upper == "A" || upper == "LEFT")           return SDL_SCANCODE_A;
        if (upper == "D" || upper == "RIGHT")          return SDL_SCANCODE_D;

        // Try SDL_GetScancodeFromName for anything else.
        return SDL_GetScancodeFromName(upper.c_str());
    };

    while (std::getline(f, line)) {
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        if (line.empty()) continue;

        // Trim
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line == "[player1]") {
            in_player1 = true;
            continue;
        }
        if (line.starts_with('[') && line.ends_with(']')) {
            in_player1 = false;
            continue;
        }

        if (!in_player1) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Trim both.
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key); trim(val);

        // Remove surrounding quotes.
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }

        keys[key] = scancode_from_name(val);
    }

    pad.set_key_mapping(keys);
}

// ===========================================================================
//  Main
// ===========================================================================

int main(int argc, char* argv[]) {
    using namespace yaps1;

    print_banner();

    // ---- Check arguments ----
    if (argc < 2) {
        std::cerr << "Usage: YAPS1 <game.bin|game.cue|game.img|game.iso>\n";
        return 1;
    }

    Path game_path = Path(argv[1]);
    if (!std::filesystem::exists(game_path)) {
        LOG_ERROR("File not found: {}", game_path.string());
        return 1;
    }

    // ---- Determine executable directory (for bios/, config.toml, etc.) ----
    Path exe_dir = std::filesystem::current_path();
    // If running from the build directory, check parent.
    if (std::filesystem::exists(exe_dir / "bios")) {
        // Already in the right directory.
    } else if (std::filesystem::exists(exe_dir / ".." / "bios")) {
        exe_dir = exe_dir / "..";
    }

    // ---- Load config ----
    Path config_path = exe_dir / "config.toml";
    auto config = Config::load(config_path);
    if (!config) {
        LOG_ERROR("{}", config.error().message);
        return 1;
    }

    // ---- Setup logger ----
    LogLevel level = LogLevel::Info;
    if (config->log_level == "debug")        level = LogLevel::Debug;
    else if (config->log_level == "warning")  level = LogLevel::Warning;
    else if (config->log_level == "error")    level = LogLevel::Error;
    Logger::instance().set_level(level);

    // Optional file logging.
    Path log_path = exe_dir / "logs" / "yaps1.log";
    std::filesystem::create_directories(log_path.parent_path());
    Logger::instance().set_file(log_path);

    // ---- Initialize SDL ----
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        LOG_WARN("SDL init warning: {}", SDL_GetError());
        // Non-fatal for terminal-only mode.
    }

    // ---- Print startup sequence ----
    LOG_INFO("Loading BIOS...");
    LOG_INFO("Loading Disc...");

    // ---- Create and initialize system ----
    auto system = std::make_unique<System>();
    auto init_result = system->init(*config, exe_dir);
    if (!init_result) {
        LOG_ERROR("{}", init_result.error().message);
        return 1;
    }

    LOG_INFO("BIOS Loaded.");
    LOG_INFO("Disc Loaded.");
    LOG_INFO("Initializing GPU...");
    LOG_INFO("Initializing SPU...");
    LOG_INFO("Initializing CPU...");

    // ---- Load game disc ----
    auto disc_result = system->load_disc(game_path);
    if (!disc_result) {
        LOG_ERROR("{}", disc_result.error().message);
        return 1;
    }

    // ---- Load controller mapping ----
    Path mapping_path = exe_dir / "mapping.toml";
    load_key_mapping(mapping_path, system->pad());

    // ---- Setup signal handlers ----
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO("Starting emulation...\n");
    LOG_INFO("Press Ctrl+C to stop.\n");

    // ---- Main loop ----
    auto frame_target_us = 1000000 / config->fps_limit;

    while (g_running.load()) {
        auto frame_start = std::chrono::steady_clock::now();

        // Run one frame of emulation.
        system->frame();

        // Frame rate limiting.
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            frame_end - frame_start).count();

        if (frame_us < static_cast<i64>(frame_target_us)) {
            i64 sleep_us = frame_target_us - frame_us;
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
    }

    // ---- Cleanup ----
    LOG_INFO("Saving memory cards...");
    system->save_memcards();

    LOG_INFO("YAPS1 shutting down.");
    SDL_Quit();

    return 0;
}

// ---- Config::generate_default_mapping (declared in config.h) ----
namespace yaps1 {

void Config::generate_default_mapping(const Path& path) {
    std::ofstream f(path);
    if (!f) return;
    f << R"([player1]
up = "W"
down = "S"
left = "A"
right = "D"
cross = "Space"
circle = "L"
square = "K"
triangle = "I"
start = "Enter"
select = "RightShift"
l1 = "Q"
r1 = "E"
l2 = "1"
r2 = "3"
)";
}

} // namespace yaps1