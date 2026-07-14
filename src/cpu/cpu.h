#pragma once

/// @file cpu.h
/// @brief MIPS R3000A CPU interpreter — the heart of YAPS1.

#include "common/types.h"
#include "common/memory_map.h"
#include "cpu/cop0.h"
#include "cpu/exceptions.h"
#include <functional>

namespace yaps1 {

class BusInterface;

/// The MIPS R3000A CPU.
/// Implements the full user-mode and kernel-mode instruction set used by
/// the PlayStation 1.  No real TLB exists on the PS1, so address translation
/// is trivial (masks).
class Cpu {
public:
    explicit Cpu(BusInterface* bus);

    /// Reset the CPU to its post-power-on state.
    void reset();

    /// Execute a single instruction.  Returns the number of cycles consumed.
    u32 step();

    /// Run for the given number of cycles (approximate).
    void run(u32 cycles);

    /// Trigger an external interrupt.  irq_bits correspond to Cause IP[9:0].
    void trigger_irq(u32 irq_bits);
    void clear_irq(u32 irq_bits);

    /// Set the initial PC (used after BIOS bootstrap).
    void set_pc(u32 addr) { pc_ = addr; }

    /// Read current state (for save-states / debug).
    [[nodiscard]] u32  pc()          const { return pc_; }
    [[nodiscard]] u32  gpr(int idx)  const { return (idx == 0) ? 0 : regs_[idx]; }
    [[nodiscard]] u32  hi()          const { return hi_; }
    [[nodiscard]] u32  lo()          const { return lo_; }
    [[nodiscard]] u64  cycles()      const { return total_cycles_; }
    [[nodiscard]] bool halted()      const { return halted_; }

    /// Load a savestate.
    void set_reg(int idx, u32 val);
    void set_hi(u32 v) { hi_ = v; }
    void set_lo(u32 v) { lo_ = v; }
    void set_cycles(u64 c) { total_cycles_ = c; }
    void set_halted(bool h) { halted_ = h; }

    /// Access COP0 for DMA / interrupt controller integration.
    [[nodiscard]] Cop0& cop0() { return cop0_; }
    [[nodiscard]] const Cop0& cop0() const { return cop0_; }

private:
    // ---- Instruction helpers ----
    u32  fetch();
    void decode_and_execute(u32 instr);
    void handle_exception(Exception exc, bool in_delay_slot = false);

    // ---- Individual instruction groups ----
    void exec_r_type(u32 instr);
    void exec_i_type(u32 instr, u32 opcode);
    void exec_j_type(u32 instr, u32 opcode);
    void exec_special2(u32 instr);   // opcode 0x1C (not common on PS1 but defined)
    void exec_cop0(u32 instr);
    void exec_cop2(u32 instr);       // GTE — stub for now

    // ---- R-type sub-functions (funct field) ----
    void op_sll(u32 instr);
    void op_srl(u32 instr);
    void op_sra(u32 instr);
    void op_sllv(u32 instr);
    void op_srlv(u32 instr);
    void op_srav(u32 instr);
    void op_jr(u32 instr);
    void op_jalr(u32 instr);
    void op_syscall(u32 instr);
    void op_break(u32 instr);
    void op_mfhi(u32 instr);
    void op_mthi(u32 instr);
    void op_mflo(u32 instr);
    void op_mtlo(u32 instr);
    void op_mult(u32 instr);
    void op_multu(u32 instr);
    void op_div(u32 instr);
    void op_divu(u32 instr);
    void op_add(u32 instr);
    void op_addu(u32 instr);
    void op_sub(u32 instr);
    void op_subu(u32 instr);
    void op_and(u32 instr);
    void op_or(u32 instr);
    void op_xor(u32 instr);
    void op_nor(u32 instr);
    void op_slt(u32 instr);
    void op_sltu(u32 instr);

    // ---- I-type helpers ----
    void exec_branch(u32 instr, bool condition);

    // ---- Load delay slot ----
    void commit_load_delay();

    // ---- Address translation (trivial on PS1) ----
    [[nodiscard]] u32 translate_addr(u32 vaddr) const;

    // ---- Data members ----
    BusInterface* bus_;

    // General-purpose registers.  Index 0 is always 0.
    std::array<u32, 32> regs_{};

    // Special registers
    u32 pc_ = 0;          // Program counter
    u32 hi_ = 0;          // Multiply/divide high
    u32 lo_ = 0;          // Multiply/divide low
    u64 total_cycles_ = 0;

    // ---- Pipeline state ----
    bool  branch_pending_  = false;
    u32   branch_target_   = 0;
    bool  in_delay_slot_   = false;

    // ---- Load delay ----
    /// PS1 has a one-instruction load delay: the loaded register is not
    /// updated until the instruction *after* the load completes.
    /// If rd == load_delay_reg_, the *old* value is read instead.
    int   load_delay_reg_  = -1;   // -1 means no pending load
    u32   load_delay_val_  = 0;

    bool  halted_ = false;

    // ---- Coprocessor 0 ----
    Cop0 cop0_;
};

} // namespace yaps1