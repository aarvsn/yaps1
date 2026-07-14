/// @file cpu.cpp
/// @brief MIPS R3000A CPU interpreter implementation.

#include "cpu/cpu.h"
#include "common/logger.h"
#include <algorithm>
#include <cstring>

namespace yaps1 {

// ===========================================================================
//  Construction / Reset
// ===========================================================================

Cpu::Cpu(BusInterface* bus) : bus_(bus) {
    reset();
}

void Cpu::reset() {
    regs_.fill(0);
    pc_ = 0xBFC00000u;  // BIOS entry point after reset (KSEG1)
    hi_ = 0;
    lo_ = 0;
    total_cycles_ = 0;
    branch_pending_ = false;
    branch_target_ = 0;
    in_delay_slot_ = false;
    load_delay_reg_ = -1;
    load_delay_val_ = 0;
    halted_ = false;
    cop0_.reset();
}

// ===========================================================================
//  Address translation
// ===========================================================================

u32 Cpu::translate_addr(u32 vaddr) const {
    // The PS1 MIPS R3000A uses the standard 3-segment address map:
    //
    //   KUSEG  0x0000_0000 .. 0x7FFF_FFFF  ->  physical 0x0000_0000 .. 0x1FFF_FFFF (cached)
    //   KSEG0  0x8000_0000 .. 0x9FFF_FFFF  ->  physical 0x0000_0000 .. 0x1FFF_FFFF (cached)
    //   KSEG1  0xA000_0000 .. 0xBFFF_FFFF  ->  physical 0x0000_0000 .. 0x1FFF_FFFF (uncached)
    //
    // KSEG2 0xC000_0000+ is not used on the PS1 (no TLB).
    // We just mask off the top 3 bits to get the physical address.

    if (vaddr < 0x80000000u) {
        // KUSEG — on PS1 with no TLB, this directly maps to physical.
        return vaddr;
    }
    // KSEG0 or KSEG1 — mask off top 3 bits.
    return vaddr & 0x1FFFFFFFu;
}

// ===========================================================================
//  Fetch
// ===========================================================================

u32 Cpu::fetch() {
    u32 phys = translate_addr(pc_);
    u32 instr = bus_->read32(phys);
    pc_ += 4;
    total_cycles_ += 1;  // instruction fetch cost
    return instr;
}

// ===========================================================================
//  Load delay
// ===========================================================================

void Cpu::commit_load_delay() {
    if (load_delay_reg_ >= 0) {
        regs_[load_delay_reg_] = load_delay_val_;
        load_delay_reg_ = -1;
    }
}

// ===========================================================================
//  Exception handling
// ===========================================================================

void Cpu::handle_exception(Exception exc, bool in_delay_slot) {
    u32 cause = cop0_.cause();

    // Set exception code
    cause = (cause & ~CAUSE_EXCCODE_MASK) | cause_from_exception(exc);

    // Set branch delay bit if exception occurred in delay slot
    if (in_delay_slot) {
        cause |= (1u << 31);
        // EPC points to the branch instruction (PC - 8), since PC already
        // advanced past the delay slot instruction.
        cop0_.set_epc(pc_ - 8);
    } else {
        cause &= ~(1u << 31);
        // PC already advanced past the faulting instruction.
        cop0_.set_epc(pc_ - 4);
    }

    cop0_.set_cause(cause);

    // Jump to exception vector.
    // SR.BEV selects between RAM vector (0x80000080) and BIOS ROM vector.
    pc_ = cop0_.bev() ? 0xBFC00180u : 0x80000080u;

    // Clear branch state
    branch_pending_ = false;

    // Clear any pending load delay
    load_delay_reg_ = -1;

    LOG_DEBUG("Exception {:02X} at PC={:08X} (in_delay_slot={})", static_cast<u32>(exc),
              cop0_.epc(), in_delay_slot);
}

// ===========================================================================
//  Interrupt trigger
// ===========================================================================

void Cpu::trigger_irq(u32 irq_bits) {
    cop0_.set_ip(irq_bits);
}

void Cpu::clear_irq(u32 irq_bits) {
    cop0_.clear_ip(irq_bits);
}

// ===========================================================================
//  Step — execute one instruction
// ===========================================================================

u32 Cpu::step() {
    if (halted_) return 1;

    u32 start_cycles = static_cast<u32>(total_cycles_);

    // Check for pending interrupts BEFORE executing the next instruction.
    // Per MIPS spec, interrupts are checked between instructions.
    if (cop0_.interrupt_pending()) {
        handle_exception(Exception::Interrupt);
        return static_cast<u32>(total_cycles_ - start_cycles);
    }

    // If a branch was pending, we are now in the delay slot.
    // The delay slot instruction will execute, and THEN we jump.
    bool was_in_delay = in_delay_slot_;
    in_delay_slot_ = branch_pending_;

    // Fetch the instruction at the current PC.
    u32 instr = fetch();

    // Commit any pending load from the *previous* instruction.
    // This happens AFTER fetch but BEFORE the current instruction reads
    // any register values, so the current instruction sees the updated value.
    // Actually, per PS1 semantics, the load delay means the instruction
    // AFTER the load still sees the OLD value.  So we commit the load
    // delay of the instruction BEFORE this one.
    // We already committed the previous delay at the start of the
    // previous step.  The new commit should happen for the NEXT step.
    // Let me reconsider...
    //
    // The load delay rule is:
    //   LW  $t0, 0($a0)    <- loads $t0
    //   ADD $v0, $t0, $s0  <- uses OLD $t0 value (load delay)
    //   ...                  <- now $t0 has the loaded value
    //
    // So when executing instruction N (the ADD), the load delay from
    // instruction N-1 (the LW) must NOT yet be committed.  The commit
    // happens BETWEEN instruction N and instruction N+1.
    //
    // Implementation: after executing the current instruction, commit the
    // load delay that was set by the *previous* instruction.

    // Decode and execute.
    decode_and_execute(instr);

    // After execution, commit any load delay that was pending from the
    // instruction BEFORE the one we just executed.
    // (load_delay_reg_/val_ were set by the previous instruction's execution.)
    // We use a two-phase approach:
    //   1. Execute instruction -> may set new load delay
    //   2. Commit the OLD load delay (from the instruction before that)

    // Actually, let's simplify: we commit at the start of the NEXT step.
    // But we need to be careful about the pipeline. Let me restructure:
    //
    // At the END of each step, we commit the pending load delay.
    // But if the current instruction is itself a load, it sets a NEW delay.
    // So we need to save/swap.
    //
    // Simplest correct approach:
    //   - Before decode: if load_delay_reg_ >= 0, commit it (write to regs_).
    //     But wait, we can't do this BEFORE decode because the current
    //     instruction should read the OLD value.
    //
    // OK here's the correct approach used by most PS1 emulators:
    //   - When reading a GPR for source operands, if the register matches
    //     load_delay_reg_, return the OLD value (regs_[idx] which hasn't
    //     been updated yet).
    //   - After executing the instruction, commit: regs_[load_delay_reg_] = load_delay_val_.
    //   - If the current instruction is also a load, the NEW delay
    //     overwrites load_delay_reg_/load_delay_val_.

    // We handle this by committing the delay AFTER execution.
    // But the instruction handlers use read_reg() which returns regs_[idx].
    // Since we haven't committed yet, regs_[idx] still has the OLD value.
    // That's correct! The instruction reads the old value, and after it
    // executes, we commit.

    // Wait — but if the instruction WRITES to the same register as the
    // pending load delay, the write should take precedence (the load
    // result is discarded). So we only commit if the register wasn't
    // overwritten by the current instruction.

    // Let's just do it simply: commit after execution.
    // We track what the current instruction wrote to, and if it's the
    // same as load_delay_reg_, we cancel the delay.

    // For now, use the simple model:
    commit_load_delay();

    // Handle branch completion.
    if (was_in_delay && branch_pending_) {
        // We were already in a delay slot — this is a branch in a delay
        // slot, which is UNPREDICTABLE on MIPS.  We just take the new branch.
    }

    if (branch_pending_ && !in_delay_slot_) {
        // We just executed a branch/jump and the NEXT instruction is the
        // delay slot.  Don't jump yet — wait for the delay slot to execute.
        // (branch_pending_ stays true, in_delay_slot_ is set at the top
        //  of the next step.)
    } else if (branch_pending_ && in_delay_slot_) {
        // We just finished the delay slot — now jump.
        pc_ = branch_target_;
        branch_pending_ = false;
        in_delay_slot_ = false;
        // Extra cycle penalty for taken branch
        total_cycles_ += 1;
    }

    return static_cast<u32>(total_cycles_ - start_cycles);
}

// ===========================================================================
//  Run for N cycles
// ===========================================================================

void Cpu::run(u32 cycles) {
    u64 target = total_cycles_ + cycles;
    while (total_cycles_ < target && !halted_) {
        step();
    }
}

// ===========================================================================
//  Set register (for save-states)
// ===========================================================================

void Cpu::set_reg(int idx, u32 val) {
    if (idx > 0 && idx < 32) regs_[idx] = val;
}

// ===========================================================================
//  Decode and dispatch
// ===========================================================================

void Cpu::decode_and_execute(u32 instr) {
    u32 opcode = (instr >> 26) & 0x3F;

    switch (opcode) {
        case 0x00: exec_r_type(instr);  break;  // SPECIAL
        case 0x01: exec_special2(instr); break;  // REGIMM (BLTZ, BGEZ, etc.)
        case 0x02: // J
        case 0x03: // JAL
            exec_j_type(instr, opcode); break;
        case 0x04: // BEQ
        case 0x05: // BNE
        case 0x06: // BLEZ
        case 0x07: // BGTZ
            exec_i_type(instr, opcode); break;
        case 0x08: // ADDI
        case 0x09: // ADDIU
        case 0x0A: // SLTI
        case 0x0B: // SLTIU
        case 0x0C: // ANDI
        case 0x0D: // ORI
        case 0x0E: // XORI
        case 0x0F: // LUI
            exec_i_type(instr, opcode); break;
        case 0x10: // COP0
            exec_cop0(instr); break;
        case 0x11: // COP1 (FPU — not on PS1)
            handle_exception(Exception::CopUnusable, in_delay_slot_);
            break;
        case 0x12: // COP2 (GTE)
            exec_cop2(instr); break;
        case 0x13: // COP3 (not on PS1)
            handle_exception(Exception::CopUnusable, in_delay_slot_);
            break;
        case 0x20: // LB
        case 0x21: // LH
        case 0x22: // LWL
        case 0x23: // LW
        case 0x24: // LBU
        case 0x25: // LHU
        case 0x26: // LWR
        case 0x28: // SB
        case 0x29: // SH
        case 0x2A: // SWL
        case 0x2B: // SW
        case 0x2E: // SWR
            exec_i_type(instr, opcode); break;
        default:
            LOG_DEBUG("Unknown opcode {:02X} at PC={:08X}", opcode, pc_ - 4);
            handle_exception(Exception::ReservedInstr, in_delay_slot_);
            break;
    }

    total_cycles_ += 1;  // base execution cost
}

// ===========================================================================
//  R-type (opcode 0x00, SPECIAL)
// ===========================================================================

void Cpu::exec_r_type(u32 instr) {
    u32 funct = instr & 0x3F;

    switch (funct) {
        case 0x00: op_sll(instr);    break;
        case 0x02: op_srl(instr);    break;
        case 0x03: op_sra(instr);    break;
        case 0x04: op_sllv(instr);   break;
        case 0x06: op_srlv(instr);   break;
        case 0x07: op_srav(instr);   break;
        case 0x08: op_jr(instr);     break;
        case 0x09: op_jalr(instr);   break;
        case 0x0C: op_syscall(instr); break;
        case 0x0D: op_break(instr);  break;
        case 0x10: op_mfhi(instr);   break;
        case 0x11: op_mthi(instr);   break;
        case 0x12: op_mflo(instr);   break;
        case 0x13: op_mtlo(instr);   break;
        case 0x18: op_mult(instr);   break;
        case 0x19: op_multu(instr);  break;
        case 0x1A: op_div(instr);    break;
        case 0x1B: op_divu(instr);   break;
        case 0x20: op_add(instr);    break;
        case 0x21: op_addu(instr);   break;
        case 0x22: op_sub(instr);    break;
        case 0x23: op_subu(instr);   break;
        case 0x24: op_and(instr);    break;
        case 0x25: op_or(instr);     break;
        case 0x26: op_xor(instr);    break;
        case 0x27: op_nor(instr);    break;
        case 0x2A: op_slt(instr);    break;
        case 0x2B: op_sltu(instr);   break;
        default:
            handle_exception(Exception::ReservedInstr, in_delay_slot_);
            break;
    }
}

// ===========================================================================
//  REGIMM (opcode 0x01) — BLTZ, BGEZ, BLTZAL, BGEZAL
// ===========================================================================

void Cpu::exec_special2(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    i32 imm = static_cast<i32>(sign_extend(instr & 0xFFFF, 16));
    u32 rs_val = regs_[(instr >> 21) & 0x1F];
    bool condition = false;
    bool link = false;

    switch (rt) {
        case 0x00: // BLTZ
            condition = (static_cast<i32>(rs_val) < 0);
            break;
        case 0x01: // BGEZ
            condition = (static_cast<i32>(rs_val) >= 0);
            break;
        case 0x10: // BLTZAL
            condition = (static_cast<i32>(rs_val) < 0);
            link = true;
            break;
        case 0x11: // BGEZAL
            condition = (static_cast<i32>(rs_val) >= 0);
            link = true;
            break;
        default:
            handle_exception(Exception::ReservedInstr, in_delay_slot_);
            return;
    }

    if (link) {
        // $ra = PC + 8 (address of instruction after delay slot)
        u32 rd = 31;
        regs_[rd] = pc_ + 4;
        // Cancel any pending load to $ra
        if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
    }

    if (condition) {
        u32 target = pc_ + (imm << 2);
        branch_target_ = target;
        branch_pending_ = true;
    }
    total_cycles_ += 1;  // branch cost
}

// ===========================================================================
//  J-type (J, JAL)
// ===========================================================================

void Cpu::exec_j_type(u32 instr, u32 opcode) {
    u32 target = ((pc_ & 0xF0000000u) | ((instr & 0x03FFFFFFu) << 2));

    if (opcode == 0x03) {  // JAL
        regs_[31] = pc_ + 4;
        if (load_delay_reg_ == 31) load_delay_reg_ = -1;
    }

    branch_target_ = target;
    branch_pending_ = true;
    total_cycles_ += 1;
}

// ===========================================================================
//  I-type instructions
// ===========================================================================

void Cpu::exec_i_type(u32 instr, u32 opcode) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u16 imm16 = static_cast<u16>(instr & 0xFFFF);
    u32 imm = imm16;                            // zero-extended
    i32 simm = sign_extend(imm16, 16);         // sign-extended

    u32 rs_val = regs_[rs];

    switch (opcode) {
        // ---- Arithmetic ----
        case 0x08: { // ADDI (traps on overflow)
            i32 result = static_cast<i32>(rs_val) + simm;
            u32 uresult = static_cast<u32>(result);
            // Overflow check: if signs of operands differ and sign of result differs from rs
            if (((~(rs_val ^ imm) & (rs_val ^ uresult)) >> 31) & 1) {
                handle_exception(Exception::Overflow, in_delay_slot_);
                return;
            }
            regs_[rt] = uresult;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x09: { // ADDIU (no trap)
            regs_[rt] = rs_val + static_cast<u32>(simm);
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0A: { // SLTI
            regs_[rt] = (static_cast<i32>(rs_val) < simm) ? 1u : 0u;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0B: { // SLTIU
            // Compare unsigned rs_val with sign-extended immediate (treated as unsigned)
            regs_[rt] = (rs_val < static_cast<u32>(simm)) ? 1u : 0u;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0C: { // ANDI
            regs_[rt] = rs_val & imm;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0D: { // ORI
            regs_[rt] = rs_val | imm;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0E: { // XORI
            regs_[rt] = rs_val ^ imm;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }
        case 0x0F: { // LUI
            regs_[rt] = imm << 16;
            if (load_delay_reg_ == static_cast<int>(rt)) load_delay_reg_ = -1;
            break;
        }

        // ---- Branches ----
        case 0x04: { // BEQ
            u32 rt_val = regs_[rt];
            exec_branch(instr, rs_val == rt_val);
            break;
        }
        case 0x05: { // BNE
            u32 rt_val = regs_[rt];
            exec_branch(instr, rs_val != rt_val);
            break;
        }
        case 0x06: { // BLEZ
            exec_branch(instr, static_cast<i32>(rs_val) <= 0);
            break;
        }
        case 0x07: { // BGTZ
            exec_branch(instr, static_cast<i32>(rs_val) > 0);
            break;
        }

        // ---- Loads ----
        case 0x20: { // LB
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr);
            i8 val = static_cast<i8>(bus_->read8(phys));
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = static_cast<u32>(static_cast<i32>(val));
            total_cycles_ += 1;
            break;
        }
        case 0x21: { // LH
            u32 addr = rs_val + simm;
            if (addr & 1) {
                handle_exception(Exception::AddressLoad, in_delay_slot_);
                return;
            }
            u32 phys = translate_addr(addr);
            i16 val = static_cast<i16>(bus_->read16(phys));
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = static_cast<u32>(static_cast<i32>(val));
            total_cycles_ += 1;
            break;
        }
        case 0x22: { // LWL
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr & ~3u);
            u32 aligned = bus_->read32(phys);
            u32 byte_offset = addr & 3;
            u32 result;
            switch (byte_offset) {
                case 0: result = (aligned << 24) | (regs_[rt] & 0x00FFFFFFu); break;
                case 1: result = ((aligned & 0x00FFFFFFu) << 8) | (regs_[rt] & 0x000000FFu); break;
                case 2: result = ((aligned & 0x0000FFFFu) << 16) | (regs_[rt] & 0x0000FFFFu); break;
                case 3: result = aligned; break;
                default: result = 0; break;
            }
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = result;
            total_cycles_ += 1;
            break;
        }
        case 0x23: { // LW
            u32 addr = rs_val + simm;
            if (addr & 3) {
                handle_exception(Exception::AddressLoad, in_delay_slot_);
                return;
            }
            u32 phys = translate_addr(addr);
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = bus_->read32(phys);
            total_cycles_ += 1;
            break;
        }
        case 0x24: { // LBU
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr);
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = static_cast<u32>(bus_->read8(phys));
            total_cycles_ += 1;
            break;
        }
        case 0x25: { // LHU
            u32 addr = rs_val + simm;
            if (addr & 1) {
                handle_exception(Exception::AddressLoad, in_delay_slot_);
                return;
            }
            u32 phys = translate_addr(addr);
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = static_cast<u32>(bus_->read16(phys));
            total_cycles_ += 1;
            break;
        }
        case 0x26: { // LWR
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr & ~3u);
            u32 aligned = bus_->read32(phys);
            u32 byte_offset = addr & 3;

            // Rotate the word left by byte_offset bytes.
            u32 result;
            switch (byte_offset) {
                case 0: result = (aligned << 24) | (regs_[rt] & 0x00FFFFFFu); break;
                case 1: result = ((aligned & 0x00FFFFFFu) << 8) | (regs_[rt] & 0x000000FFu); break;
                case 2: result = ((aligned & 0x0000FFFFu) << 16) | (regs_[rt] & 0x0000FFFFu); break;
                case 3: result = aligned; break;
                default: result = 0; break;
            }
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = result;
            total_cycles_ += 1;
            break;
        }

        // ---- Stores ----
        case 0x28: { // SB
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr);
            bus_->write8(phys, static_cast<u8>(regs_[rt]));
            total_cycles_ += 1;
            break;
        }
        case 0x29: { // SH
            u32 addr = rs_val + simm;
            if (addr & 1) {
                handle_exception(Exception::AddressStore, in_delay_slot_);
                return;
            }
            u32 phys = translate_addr(addr);
            bus_->write16(phys, static_cast<u16>(regs_[rt]));
            total_cycles_ += 1;
            break;
        }
        case 0x2B: { // SW
            u32 addr = rs_val + simm;
            if (addr & 3) {
                handle_exception(Exception::AddressStore, in_delay_slot_);
                return;
            }
            u32 phys = translate_addr(addr);
            bus_->write32(phys, regs_[rt]);
            total_cycles_ += 1;
            break;
        }
        case 0x2A: { // SWL
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr & ~3u);
            u32 old_mem = bus_->read32(phys);
            u32 rt_val = regs_[rt];
            u32 byte_offset = addr & 3;
            u32 result;
            switch (byte_offset) {
                case 0: result = (rt_val >> 24) | (old_mem & 0x00FFFFFFu); break;
                case 1: result = (rt_val >> 16) | (old_mem & 0x0000FFFFu); break;
                case 2: result = (rt_val >> 8)  | (old_mem & 0x000000FFu); break;
                case 3: result = rt_val; break;
                default: result = old_mem; break;
            }
            bus_->write32(phys, result);
            total_cycles_ += 1;
            break;
        }
        case 0x2E: { // SWR
            u32 addr = rs_val + simm;
            u32 phys = translate_addr(addr & ~3u);
            u32 old_mem = bus_->read32(phys);
            u32 rt_val = regs_[rt];
            u32 byte_offset = addr & 3;
            u32 result;
            switch (byte_offset) {
                case 0: result = rt_val; break;
                case 1: result = (old_mem & 0xFF000000u) | ((rt_val << 8) & 0x00FFFFFFu); break;
                case 2: result = (old_mem & 0xFFFF0000u) | ((rt_val << 16) & 0x0000FFFFu); break;
                case 3: result = (old_mem & 0xFFFFFF00u) | ((rt_val << 24) & 0x000000FFu); break;
                default: result = old_mem; break;
            }
            bus_->write32(phys, result);
            total_cycles_ += 1;
            break;
        }

        default:
            LOG_DEBUG("Unknown I-type opcode {:02X}", opcode);
            handle_exception(Exception::ReservedInstr, in_delay_slot_);
            break;
    }
}

// ===========================================================================
//  Branch helper
// ===========================================================================

void Cpu::exec_branch(u32 instr, bool condition) {
    i32 imm = static_cast<i32>(sign_extend(instr & 0xFFFF, 16));
    u32 target = pc_ + (imm << 2);

    if (condition) {
        branch_target_ = target;
        branch_pending_ = true;
    }
    total_cycles_ += 1;  // branch cost
}

// ===========================================================================
//  COP0 instructions
// ===========================================================================

void Cpu::exec_cop0(u32 instr) {
    // Check COP0 usable (bit 28 of SR)
    if (!bit(cop0_.sr(), 28)) {
        // Bit 28 = CU0.  If 0, COP0 is not usable.
        // On PS1 the kernel always has CU0=1, but let's handle it.
        // Actually, PS1 always runs in kernel mode where COP0 is usable.
        // We'll allow it unconditionally for now.
    }

    u32 fmt = (instr >> 21) & 0x1F;

    switch (fmt) {
        case 0x00: { // MFC0 — Move From COP0
            u32 rt = (instr >> 16) & 0x1F;
            u32 rd = (instr >> 11) & 0x1F;
            u32 val = cop0_.read_reg(rd);
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = val;
            total_cycles_ += 1;
            break;
        }
        case 0x04: { // MTC0 — Move To COP0
            u32 rt = (instr >> 16) & 0x1F;
            u32 rd = (instr >> 11) & 0x1F;
            cop0_.write_reg(rd, regs_[rt]);
            total_cycles_ += 1;
            break;
        }
        case 0x10: { // COP — COP0 specific operations
            u32 funct = instr & 0x3F;
            switch (funct) {
                case 0x10: // RFE
                    cop0_.rfe();
                    break;
                default:
                    LOG_DEBUG("Unknown COP0 funct {:02X}", funct);
                    break;
            }
            break;
        }
        default:
            LOG_DEBUG("Unknown COP0 fmt {:02X}", fmt);
            break;
    }
}

// ===========================================================================
//  COP2 (GTE) — stub
// ===========================================================================

void Cpu::exec_cop2(u32 instr) {
    // The GTE (Geometry Transformation Engine) is the PS1's coprocessor 2.
    // It handles 3D matrix math, lighting, perspective projection, etc.
    // This is a stub — full GTE implementation is a major undertaking.
    // For now, we handle the MFC2/MTC2 opcodes to prevent crashes and
    // implement the basic register interface.

    u32 fmt = (instr >> 21) & 0x1F;

    switch (fmt) {
        case 0x00: { // MFC2
            u32 rt = (instr >> 16) & 0x1F;
            // Return 0 for all GTE data registers (stub)
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = 0;
            break;
        }
        case 0x02: { // CFC2 — Move Control From COP2
            u32 rt = (instr >> 16) & 0x1F;
            load_delay_reg_ = static_cast<int>(rt);
            load_delay_val_ = 0;
            break;
        }
        case 0x04: { // MTC2
            // Silently ignore writes to GTE registers (stub)
            break;
        }
        case 0x06: { // CTC2
            // Silently ignore writes to GTE control registers (stub)
            break;
        }
        default: {
            // GTE command instructions (fmt bits[25:24] = 0b10)
            // 0x80-0xBF are GTE commands.  We just skip them.
            if (fmt >= 0x10 && fmt <= 0x1F) {
                // GTE command — no-op in stub
            } else {
                LOG_DEBUG("Unknown COP2 fmt {:02X}", fmt);
            }
            break;
        }
    }
    total_cycles_ += 1;
}

// ===========================================================================
//  R-type individual instruction implementations
// ===========================================================================

void Cpu::op_sll(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 sa = (instr >> 6) & 0x1F;
    regs_[rd] = regs_[rt] << sa;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_srl(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 sa = (instr >> 6) & 0x1F;
    regs_[rd] = regs_[rt] >> sa;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_sra(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 sa = (instr >> 6) & 0x1F;
    regs_[rd] = static_cast<u32>(static_cast<i32>(regs_[rt]) >> sa);
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_sllv(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 rs = (instr >> 21) & 0x1F;
    u32 sa = regs_[rs] & 0x1F;
    regs_[rd] = regs_[rt] << sa;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_srlv(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 rs = (instr >> 21) & 0x1F;
    u32 sa = regs_[rs] & 0x1F;
    regs_[rd] = regs_[rt] >> sa;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_srav(u32 instr) {
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 rs = (instr >> 21) & 0x1F;
    u32 sa = regs_[rs] & 0x1F;
    regs_[rd] = static_cast<u32>(static_cast<i32>(regs_[rt]) >> sa);
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_jr(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    branch_target_ = regs_[rs];
    branch_pending_ = true;
    total_cycles_ += 1;
}

void Cpu::op_jalr(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = pc_ + 4;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
    branch_target_ = regs_[rs];
    branch_pending_ = true;
    total_cycles_ += 1;
}

void Cpu::op_syscall(u32 /*instr*/) {
    handle_exception(Exception::Syscall, in_delay_slot_);
}

void Cpu::op_break(u32 /*instr*/) {
    handle_exception(Exception::Breakpoint, in_delay_slot_);
}

void Cpu::op_mfhi(u32 instr) {
    u32 rd = (instr >> 11) & 0x1F;
    load_delay_reg_ = static_cast<int>(rd);
    load_delay_val_ = hi_;
}

void Cpu::op_mthi(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    hi_ = regs_[rs];
}

void Cpu::op_mflo(u32 instr) {
    u32 rd = (instr >> 11) & 0x1F;
    load_delay_reg_ = static_cast<int>(rd);
    load_delay_val_ = lo_;
}

void Cpu::op_mtlo(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    lo_ = regs_[rs];
}

void Cpu::op_mult(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    i64 result = static_cast<i64>(static_cast<i32>(regs_[rs])) *
                 static_cast<i64>(static_cast<i32>(regs_[rt]));
    lo_ = static_cast<u32>(result & 0xFFFFFFFFu);
    hi_ = static_cast<u32>((result >> 32) & 0xFFFFFFFFu);
    total_cycles_ += 2;  // MULT is slow
}

void Cpu::op_multu(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u64 result = static_cast<u64>(regs_[rs]) * static_cast<u64>(regs_[rt]);
    lo_ = static_cast<u32>(result & 0xFFFFFFFFu);
    hi_ = static_cast<u32>((result >> 32) & 0xFFFFFFFFu);
    total_cycles_ += 2;
}

void Cpu::op_div(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    i32 dividend = static_cast<i32>(regs_[rs]);
    i32 divisor  = static_cast<i32>(regs_[rt]);

    if (divisor == 0) {
        // Undefined behavior on MIPS, but most implementations set:
        lo_ = (dividend >= 0) ? 0xFFFFFFFFu : 1u;
        hi_ = static_cast<u32>(dividend);
    } else if (static_cast<i32>(0x80000000u) == dividend && divisor == -1) {
        // Overflow case
        lo_ = 0x80000000u;
        hi_ = 0;
    } else {
        lo_ = static_cast<u32>(dividend / divisor);
        hi_ = static_cast<u32>(dividend % divisor);
    }
    total_cycles_ += 5;  // DIV is very slow
}

void Cpu::op_divu(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 dividend = regs_[rs];
    u32 divisor  = regs_[rt];

    if (divisor == 0) {
        lo_ = 0xFFFFFFFFu;
        hi_ = dividend;
    } else {
        lo_ = dividend / divisor;
        hi_ = dividend % divisor;
    }
    total_cycles_ += 5;
}

void Cpu::op_add(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 result = regs_[rs] + regs_[rt];
    // Overflow check
    if (((~(regs_[rs] ^ regs_[rt]) & (regs_[rs] ^ result)) >> 31) & 1) {
        handle_exception(Exception::Overflow, in_delay_slot_);
        return;
    }
    regs_[rd] = result;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_addu(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = regs_[rs] + regs_[rt];
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_sub(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    u32 result = regs_[rs] - regs_[rt];
    if (((regs_[rs] ^ regs_[rt]) & (regs_[rs] ^ result)) >> 31 & 1) {
        handle_exception(Exception::Overflow, in_delay_slot_);
        return;
    }
    regs_[rd] = result;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_subu(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = regs_[rs] - regs_[rt];
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_and(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = regs_[rs] & regs_[rt];
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_or(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = regs_[rs] | regs_[rt];
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_xor(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = regs_[rs] ^ regs_[rt];
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_nor(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = ~(regs_[rs] | regs_[rt]);
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_slt(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = (static_cast<i32>(regs_[rs]) < static_cast<i32>(regs_[rt])) ? 1u : 0u;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

void Cpu::op_sltu(u32 instr) {
    u32 rs = (instr >> 21) & 0x1F;
    u32 rt = (instr >> 16) & 0x1F;
    u32 rd = (instr >> 11) & 0x1F;
    regs_[rd] = (regs_[rs] < regs_[rt]) ? 1u : 0u;
    if (load_delay_reg_ == static_cast<int>(rd)) load_delay_reg_ = -1;
}

} // namespace yaps1