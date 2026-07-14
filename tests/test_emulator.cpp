#include "cpu/cpu.h"
#include "cpu/cop0.h"
#include "common/memory_map.h"
#include <iostream>
#include <cassert>
#include <array>

using namespace yaps1;

class MockBus final : public BusInterface {
public:
    MockBus() {
        mem_.fill(0);
    }

    u8  read8(u32 addr) override {
        u32 offset = addr % mem_.size();
        return mem_[offset];
    }

    u16 read16(u32 addr) override {
        u32 offset = addr % mem_.size();
        return mem_[offset] | (mem_[offset + 1] << 8);
    }

    u32 read32(u32 addr) override {
        u32 offset = addr % mem_.size();
        return mem_[offset] | (mem_[offset + 1] << 8) | (mem_[offset + 2] << 16) | (mem_[offset + 3] << 24);
    }

    void write8(u32 addr, u8 val) override {
        u32 offset = addr % mem_.size();
        mem_[offset] = val;
    }

    void write16(u32 addr, u16 val) override {
        u32 offset = addr % mem_.size();
        mem_[offset] = val & 0xFF;
        mem_[offset + 1] = (val >> 8) & 0xFF;
    }

    void write32(u32 addr, u32 val) override {
        u32 offset = addr % mem_.size();
        mem_[offset] = val & 0xFF;
        mem_[offset + 1] = (val >> 8) & 0xFF;
        mem_[offset + 2] = (val >> 16) & 0xFF;
        mem_[offset + 3] = (val >> 24) & 0xFF;
    }

    void read_block(u32 /*addr*/, ByteSpan& /*dst*/) override {}
    void write_block(u32 /*addr*/, ByteSpan /*src*/) override {}

    void set_instr(u32 addr, u32 instr) {
        write32(addr, instr);
    }

private:
    std::array<u8, 1 * MIB> mem_;
};

// Test 1: Verify COP0 Status Register stack shifts correctly on rfe()
void test_cop0_rfe() {
    std::cout << "[Test] Running test_cop0_rfe..." << std::endl;
    Cop0 cop0;
    cop0.reset();

    // Reset status register:
    // Status Register value with:
    //   KUo = 1, IEo = 0   (bits 5:4 = 0b10 -> 0x20)
    //   KUp = 0, IEp = 1   (bits 3:2 = 0b01 -> 0x04)
    //   KUc = 1, IEc = 0   (bits 1:0 = 0b10 -> 0x02)
    // So lower 6 bits = 0x20 | 0x04 | 0x02 = 0x26
    u32 initial_sr = (cop0.sr() & ~0x3Fu) | 0x26u;
    cop0.write_reg(Cop0::SR, initial_sr);

    assert(cop0.ie_current() == false);
    assert(cop0.ku_current() == true);

    cop0.rfe();

    // After RFE:
    //   KUc <- KUp (0)
    //   IEc <- IEp (1)
    //   KUp <- KUo (1)
    //   IEp <- IEo (0)
    //   KUo and IEo remain unchanged (1, 0)
    // Expected lower 6 bits stack:
    //   KUo = 1, IEo = 0
    //   KUp = 1, IEp = 0
    //   KUc = 0, IEc = 1
    // Lower 6 bits value: (0x20) | (0x08) | (0x01) = 0x29
    u32 updated_sr = cop0.sr() & 0x3Fu;
    std::cout << "  Initial lower 6 bits: 0x" << std::hex << 0x26 << ", updated: 0x" << updated_sr << std::dec << std::endl;
    assert(updated_sr == 0x29u);
    assert(cop0.ie_current() == true);
    assert(cop0.ku_current() == false);

    std::cout << "  test_cop0_rfe PASSED!" << std::endl;
}

// Test 2: Verify CPU REGIMM branch logic (BLTZ, BGEZ) only branches when condition is met
void test_cpu_special2_branching() {
    std::cout << "[Test] Running test_cpu_special2_branching..." << std::endl;
    MockBus bus;
    Cpu cpu(&bus);

    // Register 8 ($t0) is used for rs
    // BLTZ instruction format: opcode 0x01, rs 0x08, rt 0x00, imm 0x0005
    // instr: (0x01 << 26) | (0x08 << 21) | (0x00 << 16) | 0x0005 = 0x05000005
    u32 bltz_instr = (0x01u << 26) | (0x08u << 21) | (0x00u << 16) | 0x0005u;

    // BGEZ instruction format: opcode 0x01, rs 0x08, rt 0x01, imm 0x0005
    // instr: (0x01 << 26) | (0x08 << 21) | (0x01 << 16) | 0x0005 = 0x05010005
    u32 bgez_instr = (0x01u << 26) | (0x08u << 21) | (0x01u << 16) | 0x0005u;

    // Case 2a: BLTZ with positive value (should NOT branch)
    cpu.reset();
    cpu.set_reg(8, 42); // $t0 = 42
    bus.set_instr(0xBFC00000u, bltz_instr);
    bus.set_instr(0xBFC00004u, 0); // delay slot (NOP)

    // Execute the BLTZ
    cpu.step();
    // Execute the delay slot instruction
    cpu.step();

    // Since we did not branch, PC should be 0xBFC00008
    std::cout << "  BLTZ with positive: PC = 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    assert(cpu.pc() == 0xBFC00008u);

    // Case 2b: BLTZ with negative value (should branch)
    cpu.reset();
    cpu.set_reg(8, static_cast<u32>(-42)); // $t0 = -42
    bus.set_instr(0xBFC00000u, bltz_instr);
    bus.set_instr(0xBFC00004u, 0); // delay slot

    std::cout << "  Executing BLTZ: register 8 is " << static_cast<i32>(cpu.gpr(8)) << ", PC is 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    cpu.step(); // executes BLTZ, sets branch_pending_
    std::cout << "  After BLTZ step 1: PC is 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    cpu.step(); // executes delay slot, performs jump to target: PC + 4 + (imm << 2) -> 0xBFC00004 + (5 << 2) = 0xBFC00018
    std::cout << "  BLTZ with negative: PC = 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    assert(cpu.pc() == 0xBFC00018u);

    // Case 2c: BGEZ with positive value (should branch)
    cpu.reset();
    cpu.set_reg(8, 100); // $t0 = 100
    bus.set_instr(0xBFC00000u, bgez_instr);
    bus.set_instr(0xBFC00004u, 0); // delay slot

    cpu.step();
    cpu.step();
    std::cout << "  BGEZ with positive: PC = 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    assert(cpu.pc() == 0xBFC00018u);

    // Case 2d: BGEZ with negative value (should NOT branch)
    cpu.reset();
    cpu.set_reg(8, static_cast<u32>(-5)); // $t0 = -5
    bus.set_instr(0xBFC00000u, bgez_instr);
    bus.set_instr(0xBFC00004u, 0); // delay slot

    cpu.step();
    cpu.step();
    std::cout << "  BGEZ with negative: PC = 0x" << std::hex << cpu.pc() << std::dec << std::endl;
    assert(cpu.pc() == 0xBFC00008u);

    std::cout << "  test_cpu_special2_branching PASSED!" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "             YAPS1 EMULATOR TEST SUITE              " << std::endl;
    std::cout << "====================================================" << std::endl;

    test_cop0_rfe();
    test_cpu_special2_branching();

    std::cout << "\nAll emulator unit tests passed successfully!" << std::endl;
    return 0;
}