#pragma once

/// @file logger.h
/// @brief Simple terminal + optional file logger.

#include "common/types.h"
#include <fmt/format.h>
#include <mutex>
#include <fstream>
#include <iostream>

namespace yaps1 {

enum class LogLevel {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3,
    None    = 4,
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) { level_ = level; }
    void set_file(Path path) {
        std::lock_guard lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
    }

    template <typename... Args>
    void debug(fmt::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, fmt::vformat(fmt, fmt::make_format_args(args...)));
    }

    template <typename... Args>
    void info(fmt::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, fmt::vformat(fmt, fmt::make_format_args(args...)));
    }

    template <typename... Args>
    void warn(fmt::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warning, fmt::vformat(fmt, fmt::make_format_args(args...)));
    }

    template <typename... Args>
    void error(fmt::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, fmt::vformat(fmt, fmt::make_format_args(args...)));
    }

private:
    Logger() = default;

    void log(LogLevel lvl, std::string msg) {
        if (lvl < level_) return;
        std::lock_guard lock(mutex_);

        const char* tag = "?";
        const char* ansi = "\033[0m";  // reset
        switch (lvl) {
            case LogLevel::Debug:   tag = "DEBUG";   ansi = "\033[36m"; break; // cyan
            case LogLevel::Info:    tag = "INFO";    ansi = "\033[32m"; break; // green
            case LogLevel::Warning: tag = "WARNING"; ansi = "\033[33m"; break; // yellow
            case LogLevel::Error:   tag = "ERROR";   ansi = "\033[31m"; break; // red
            default: break;
        }

        std::string line = fmt::format("[{}] {}", tag, msg);
        // Write colored to stderr, plain to file.
        std::cerr << ansi << line << "\033[0m" << '\n';

        if (file_.is_open()) {
            file_ << line << '\n';
            file_.flush();
        }
    }

    LogLevel level_ = LogLevel::Info;
    std::mutex mutex_;
    std::ofstream file_;
};

// Convenience macros
#define LOG_DEBUG(...)   yaps1::Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...)    yaps1::Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...)    yaps1::Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...)   yaps1::Logger::instance().error(__VA_ARGS__)

} // namespace yaps1