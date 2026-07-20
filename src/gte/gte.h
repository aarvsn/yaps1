#pragma once

#include "common/types.h"
#include <array>

namespace yaps1 {

class BusInterface;

class Gte {
public:
    Gte();
    ~Gte() = default;

    void set_bus(BusInterface* bus) { bus_ = bus; }
    void reset();

    u32 read_data(u32 reg);
    void write_data(u32 reg, u32 val);
    u32 read_ctrl(u32 reg);
    void write_ctrl(u32 reg, u32 val);

    void execute(u32 opcode);

private:
    void rtv0(u32 opcode);
    void rtv1(u32 opcode);
    void rtv2(u32 opcode);
    void rtps();
    void rtpt();
    void mvmva(u32 opcode);
    void nclip();
    void avsz3();
    void avsz4();
    void dpcs();
    void dpct();
    void ncds();
    void ncdt();
    void ncss();
    void ncst();
    void op_sqr(u32 opcode);
    void op_dcpl(u32 opcode);
    void gpf(u32 opcode);
    void gpl(u32 opcode);
    void op_intpl();
    void op_cc(u32 opcode);

    i32 sat(i64 v, i32 max);
    i32 sat5(i32 v);
    i32 sat9(i32 v);
    i32 sat12(i32 v);
    i32 lim(i64 v, i32 max);
    i32 nlim(i64 v);

    void push_sxy();

    static i64 sign16(u32 v);
    static i64 sign11(u32 v);

    BusInterface* bus_ = nullptr;

    std::array<u32, 32> dr_{};
    std::array<u32, 32> cr_{};

    u32 opcode_;
    u32 cmd_;
    u32 funct_;
    u32 sf_;
};

} // namespace yaps1
