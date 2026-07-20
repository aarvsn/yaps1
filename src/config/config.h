#pragma once

/// @file config.h
/// @brief TOML-based configuration loader/generator.

#include "common/types.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace yaps1 {

struct Config {
    // Video
    bool   fullscreen         = false;
    std::string renderer      = "software";  // "software" or "vulkan"
    bool   vsync              = true;
    u32    internal_resolution = 1;
    bool   show_fps           = false;

    // Audio
    u32    audio_volume       = 100;

    // BIOS / Region
    std::string bios          = "auto";  // "auto" or filename
    std::string region        = "auto";  // "auto", "NTSC-J", "NTSC-U", "PAL"

    // Emulation
    u32    fps_limit          = 60;
    bool   rewind             = false;
    std::string log_level     = "info";

    /// Load from config.toml. If the file doesn't exist, generate defaults.
    static Result<Config> load(const Path& path);

    /// Generate a default config.toml at the given path.
    static void generate_default(const Path& path);

    /// Generate a default mapping.toml at the given path.
    static void generate_default_mapping(const Path& path);
};

// ---- Minimal TOML parser (no external dependency) ----
// PS1 emu config is flat key=value so we only need that subset.

namespace toml {

struct Value {
    enum Type { Bool, Int, String, None } type = None;
    bool        bval = false;
    i64         ival = 0;
    std::string sval;
};

using Table = std::unordered_map<std::string, Value>;

inline Result<Table> parse(std::string_view src) {
    Table result;
    std::istringstream stream{std::string(src)};
    std::string line;

    while (std::getline(stream, line)) {
        // Trim whitespace
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string raw = line.substr(eq + 1);

        // Trim
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
        };
        trim(key); trim(raw);

        Value v;
        if (raw == "true")       { v.type = Value::Bool; v.bval = true; }
        else if (raw == "false") { v.type = Value::Bool; v.bval = false; }
        else if (raw.starts_with('"') && raw.ends_with('"')) {
            v.type = Value::String;
            v.sval = raw.substr(1, raw.size() - 2);
        }
        else {
            v.type = Value::Int;
            try { v.ival = std::stoll(raw); } catch (...) { continue; }
        }
        result[key] = v;
    }
    return result;
}

} // namespace toml

inline void Config::generate_default(const Path& path) {
    std::ofstream f(path);
    if (!f) return;
    f << R"(# YAPS1 Configuration
# Lines starting with # are comments.

# Video settings
fullscreen = false
renderer = "software"        # "software" or "vulkan"
vsync = true
internal_resolution = 1      # 1 = native, 2 = 2x, 4 = 4x, etc.
show_fps = false

# Audio settings
audio_volume = 100            # 0-100

# BIOS / Region
bios = "auto"                 # "auto" or a specific filename like "SCPH1001.BIN"
region = "auto"               # "auto", "NTSC-J", "NTSC-U", "PAL"

# Emulation settings
fps_limit = 60
rewind = false
log_level = "info"            # "debug", "info", "warning", "error"
)";
}

inline Result<Config> Config::load(const Path& path) {
    Config cfg;

    if (!std::filesystem::exists(path)) {
        generate_default(path);
        return cfg;
    }

    std::ifstream f(path);
    if (!f) return std::unexpected(Error{"Cannot open config.toml"});

    std::ostringstream ss;
    ss << f.rdbuf();
    auto table = toml::parse(ss.str());
    if (!table) return std::unexpected(table.error());

    auto get_bool = [&](const std::string& k, bool def) -> bool {
        auto it = table->find(k);
        if (it == table->end() || it->second.type != toml::Value::Bool) return def;
        return it->second.bval;
    };
    auto get_int = [&](const std::string& k, i64 def) -> i64 {
        auto it = table->find(k);
        if (it == table->end() || it->second.type != toml::Value::Int) return def;
        return it->second.ival;
    };
    auto get_str = [&](const std::string& k, const std::string& def) -> std::string {
        auto it = table->find(k);
        if (it == table->end() || it->second.type != toml::Value::String) return def;
        return it->second.sval;
    };

    cfg.fullscreen          = get_bool("fullscreen", false);
    cfg.renderer            = get_str("renderer", "software");
    cfg.vsync               = get_bool("vsync", true);
    cfg.internal_resolution = static_cast<u32>(get_int("internal_resolution", 1));
    cfg.show_fps            = get_bool("show_fps", false);
    cfg.audio_volume        = static_cast<u32>(get_int("audio_volume", 100));
    cfg.bios                = get_str("bios", "auto");
    cfg.region              = get_str("region", "auto");
    cfg.fps_limit           = static_cast<u32>(get_int("fps_limit", 60));
    cfg.rewind              = get_bool("rewind", false);
    cfg.log_level           = get_str("log_level", "info");

    return cfg;
}

} // namespace yaps1