#pragma once

/// @file cop0.h
/// @brief COP0 — System Control Coprocessor for the MIPS R3000A.
///
/// COP0 manages the MMU, exceptions, and interrupts.  On the PS1 there is
/// no real TLB (all memory is unmapped), so only the interrupt / exception
/// registers are meaningful.

#include "common/types.h"
#include <array>

namespace yaps1 {

class Cpu;  // forward decl

class Cop0 {
public:
    void reset();

    // ---- Register indices ----
    enum Reg : u32 {
        BPC    = 3,   // Breakpoint (debug)
        BDA    = 5,   // Breakpoint data address (debug)
        DCIC   = 7,   // Breakpoint control (debug)
        SR     = 12,  // Status Register
        CAUSE  = 13,  // Cause Register
        EPC    = 14,  // Exception Program Counter
        PRID   = 15,  // Processor Revision ID
    };

    // ---- Read / Write ----
    u32 read_reg(u32 idx) const;
    void write_reg(u32 idx, u32 val);

    // ---- Convenience accessors ----
    [[nodiscard]] u32 sr()    const { return regs_[SR]; }
    [[nodiscard]] u32 cause() const { return regs_[CAUSE]; }
    [[nodiscard]] u32 epc()   const { return regs_[EPC]; }

    void set_cause(u32 val)   { regs_[CAUSE] = val; }
    void set_epc(u32 val)     { regs_[EPC] = val; }

    // ---- Status Register bit helpers ----
    /// Is the current interrupt-enable bit set?
    [[nodiscard]] bool ie_current() const { return bit(regs_[SR], 0); }
    /// Current kernel/user mode: false = kernel, true = user.
    [[nodiscard]] bool ku_current() const { return bit(regs_[SR], 1); }

    // ---- Cause register helpers ----
    /// Get the exception code field (bits 6:2).
    [[nodiscard]] u32 exception_code() const { return (regs_[CAUSE] >> 2) & 0x1F; }
    /// Was the exception taken in a branch delay slot?
    [[nodiscard]] bool branch_delay() const { return bit(regs_[CAUSE], 31); }

    // ---- Pending / mask ----
    /// Set bits in the Interrupt Pending field (bits 9:8).
    void set_ip(u32 irq_bits);
    void clear_ip(u32 irq_bits);

    /// Are any unmasked interrupts pending?
    [[nodiscard]] bool interrupt_pending() const;

    // ---- BEV ----
    [[nodiscard]] bool bev() const { return bit(regs_[SR], 22); }

    // ---- RFE ----
    /// Return From Exception: shift SR bits back.
    /// IEc <- IEp, KUc <- KUp, IEp <- IEo, KUp <- KUo
    void rfe();

private:
    std::array<u32, 32> regs_{};

    static constexpr u32 PRID_VALUE = 0x00000002;  // R3000A revision
};

} // namespace yaps1