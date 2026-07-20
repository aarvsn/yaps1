#pragma once

/// @file types.h
/// @brief Fundamental type aliases used throughout YAPS1.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <functional>
#include <expected>
#include <filesystem>

namespace yaps1 {

// ------------------------------------------------------------------ integers
using  u8  = std::uint8_t;
using  u16 = std::uint16_t;
using  u32 = std::uint32_t;
using  u64 = std::uint64_t;
using  i8  = std::int8_t;
using  i16 = std::int16_t;
using  i32 = std::int32_t;
using  i64 = std::int64_t;
using byte = u8;

// ------------------------------------------------------------------ aliases
using Path = std::filesystem::path;

// A non-owning view into a contiguous block of bytes.
using ByteSpan = std::span<const byte>;

// Owning byte buffer.
using ByteVec = std::vector<byte>;

// ------------------------------------------------------------------ result
/// Lightweight error type — just a string description.
struct Error {
    std::string message;
};

/// A result that is either a value T or an Error.
template <typename T>
using Result = std::expected<T, Error>;

// ------------------------------------------------------------------ helpers
[[nodiscard]] constexpr bool bit(u32 val, int n) { return (val >> n) & 1u; }
[[nodiscard]] constexpr u32  bits(u32 val, int hi, int lo) {
    return (val >> lo) & ((1u << (hi - lo + 1)) - 1u);
}
[[nodiscard]] constexpr u32  sign_extend(u32 val, int bits) {
    u32 sign = 1u << (bits - 1);
    return (val ^ sign) - sign;
}
[[nodiscard]] constexpr u32  align_up(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }
[[nodiscard]] constexpr u32  clamp(u32 v, u32 lo, u32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace yaps1