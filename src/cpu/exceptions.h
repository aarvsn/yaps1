#pragma once

/// @file exceptions.h
/// @brief MIPS R3000A exception codes and handling utilities.

#include "common/types.h"

namespace yaps1 {

/// PS1 / MIPS exception codes (stored in COP0 Cause register bits 6:2).
enum class Exception : u32 {
    Interrupt     = 0x00,  // External interrupt
    TLBModify     = 0x01,  // TLB modification
    TLBLoad       = 0x02,  // TLB miss on load
    TLBStore      = 0x03,  // TLB miss on store
    AddressLoad   = 0x04,  // Address error on load
    AddressStore  = 0x05,  // Address error on store
    BusInstr      = 0x06,  // Bus error (instruction fetch)
    BusData       = 0x07,  // Bus error (data access)
    Syscall       = 0x08,  // SYSCALL instruction
    Breakpoint    = 0x09,  // BREAK instruction
    ReservedInstr = 0x0A,  // Reserved instruction
    CopUnusable   = 0x0B,  // Coprocessor unusable
    Overflow      = 0x0C,  // Arithmetic overflow
};

/// Bitmask to shift the exception code into the Cause register.
inline constexpr u32 CAUSE_EXCCODE_SHIFT = 2;
inline constexpr u32 CAUSE_EXCCODE_MASK  = 0x1F << CAUSE_EXCCODE_SHIFT;

/// Convert an Exception enum to the bit field value stored in Cause[6:2].
[[nodiscard]] constexpr u32 cause_from_exception(Exception e) {
    return static_cast<u32>(e) << CAUSE_EXCCODE_SHIFT;
}

/// Extract the exception code from a Cause register value.
[[nodiscard]] constexpr Exception exception_from_cause(u32 cause) {
    return static_cast<Exception>((cause >> CAUSE_EXCCODE_SHIFT) & 0x1F);
}

/// Exception vector addresses (KSEG0).
/// PS1 uses the general exception vector at 0x8000_0080.
/// When BEV=1 in SR, vectors point to the BIOS ROM area 0xBFC0_0180.
inline constexpr u32 EXCEPTION_VECTOR       = 0x80000080u;
inline constexpr u32 EXCEPTION_VECTOR_BEV   = 0xBFC00180u;

} // namespace yaps1