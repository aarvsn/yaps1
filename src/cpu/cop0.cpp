/// @file cop0.cpp
/// @brief COP0 implementation.

#include "cpu/cop0.h"

namespace yaps1 {

void Cop0::reset() {
    regs_.fill(0);
    // Processor ID for R3000A
    regs_[PRID] = PRID_VALUE;
    // After reset the PS1 boots in kernel mode, IE disabled, BEV=1
    // (BIOS exception vectors active).
    regs_[SR] = (1u << 22)  // BEV = 1
              | (1u << 1);  // KUc = 1 (user — BIOS clears this early)
}

u32 Cop0::read_reg(u32 idx) const {
    if (idx < 32) return regs_[idx];
    return 0;
}

void Cop0::write_reg(u32 idx, u32 val) {
    if (idx >= 32) return;

    // PRID is read-only
    if (idx == PRID) return;

    // CAUSE register: only certain bits are writable.
    // Bits 8:9 (IP) are written by hardware; software cannot write them.
    if (idx == CAUSE) {
        // Preserve the IP bits, allow software to write the rest.
        u32 ip_mask = 0xFF00u;  // bits 15:8
        regs_[CAUSE] = (val & ~ip_mask) | (regs_[CAUSE] & ip_mask);
        return;
    }

    regs_[idx] = val;
}

void Cop0::set_ip(u32 irq_bits) {
    // IRQ bits sit at Cause[15:8] — but only bits 9:8 are the PS1 IRQ lines.
    regs_[CAUSE] |= (irq_bits & 0xFF) << 8;
}

void Cop0::clear_ip(u32 irq_bits) {
    regs_[CAUSE] &= ~((irq_bits & 0xFF) << 8);
}

bool Cop0::interrupt_pending() const {
    // Interrupts pending = Cause.IP & Status.IM
    u32 ip = (regs_[CAUSE] >> 8) & 0xFF;
    u32 im = (regs_[SR]    >> 8) & 0xFF;
    bool pending = (ip & im) != 0;
    // Also check the global interrupt enable bit (SR bit 0)
    return pending && ie_current();
}

void Cop0::rfe() {
    // Return From Exception shifts the stack:
    //   IEc <- IEp,  KUc <- KUp
    //   IEp <- IEo,  KUp <- KUo
    u32 sr = regs_[SR];
    // Lower 6 bits are: KUo, IEo, KUp, IEp, KUc, IEc (bits 5:0)
    u32 stack = sr & 0x3Fu;
    // Shift the stack right by 2 bits to pop: KUc, IEc get KUp, IEp. KUp, IEp get KUo, IEo.
    u32 shifted_stack = (stack >> 2) & 0xFu;
    // KUo and IEo (bits 5:4) remain unchanged.
    u32 unchanged_part = stack & 0x30u;

    regs_[SR] = (sr & ~0x3Fu) | unchanged_part | shifted_stack;
}

} // namespace yaps1