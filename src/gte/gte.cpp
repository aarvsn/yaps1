#include "gte/gte.h"
#include "common/logger.h"
#include <algorithm>
#include <cstring>

namespace yaps1 {

Gte::Gte() { reset(); }

void Gte::reset() {
    dr_.fill(0);
    cr_.fill(0);
}

i64 Gte::sign16(u32 v) {
    return static_cast<i64>(static_cast<i16>(v & 0xFFFF));
}

i64 Gte::sign11(u32 v) {
    i64 r = v & 0x7FF;
    if (v & 0x400) r |= ~0x7FFULL;
    return r;
}

i32 Gte::sat(i64 v, i32 max) {
    if (v > max) return max;
    if (v < -max - 1) return -max - 1;
    return static_cast<i32>(v);
}

i32 Gte::sat5(i32 v) {
    return std::clamp(v, -16, 15);
}

i32 Gte::sat9(i32 v) {
    return std::clamp(v, -256, 255);
}

i32 Gte::sat12(i32 v) {
    return std::clamp(v, -2048, 2047);
}

i32 Gte::lim(i64 v, i32 max) {
    if (v > static_cast<i64>(max)) return max;
    if (v < 0) return 0;
    return static_cast<i32>(v);
}

i32 Gte::nlim(i64 v) {
    if (v > 0xFFFFFFFFLL) return 0xFFFFFFFF;
    if (v < 0) return 0;
    return static_cast<i32>(v & 0xFFFFFFFF);
}

// ================================
//  Register access
// ================================

u32 Gte::read_data(u32 reg) {
    if (reg > 31) return 0;

    switch (reg) {
        case 1:
        case 3:
        case 5:
            return dr_[reg] & 0xFFFF;
        case 7: {
            u32 otz = dr_[7];
            if (otz > 0xFFFF) otz = 0xFFFF;
            return otz;
        }
        case 15: {
            return dr_[15];
        }
        default:
            return dr_[reg];
    }
}

void Gte::write_data(u32 reg, u32 val) {
    if (reg > 31) return;
    dr_[reg] = val;
}

u32 Gte::read_ctrl(u32 reg) {
    if (reg > 31) return 0;
    return cr_[reg];
}

void Gte::write_ctrl(u32 reg, u32 val) {
    if (reg > 31) return;

    switch (reg) {
        case 4:
            cr_[4] = val & 0xFFFF;
            cr_[5] = (val >> 16) & 0xFFFF;
            break;
        case 6:
            cr_[6] = val & 0xFFFF;
            cr_[7] = (val >> 16) & 0xFFFF;
            break;
        case 8:
            cr_[8] = val & 0xFFFF;
            cr_[9] = (val >> 16) & 0xFFFF;
            break;
        case 10:
            cr_[10] = val & 0xFFFF;
            cr_[11] = (val >> 16) & 0xFFFF;
            break;
        case 12:
            cr_[12] = val & 0xFFFF;
            cr_[13] = (val >> 16) & 0xFFFF;
            break;
        case 14:
            cr_[14] = val & 0xFFFF;
            cr_[15] = (val >> 16) & 0xFFFF;
            break;
        case 16:
            cr_[16] = val & 0xFFFF;
            cr_[17] = (val >> 16) & 0xFFFF;
            break;
        case 18:
            cr_[18] = val & 0xFFFF;
            cr_[19] = (val >> 16) & 0xFFFF;
            break;
        case 20:
            cr_[20] = val & 0xFFFF;
            cr_[21] = (val >> 16) & 0xFFFF;
            break;
        case 22:
            cr_[22] = val & 0xFFFF;
            cr_[23] = (val >> 16) & 0xFFFF;
            break;
        case 24:
            cr_[24] = val & 0xFFFF;
            cr_[25] = (val >> 16) & 0xFFFF;
            break;
        case 26:
            cr_[26] = val & 0xFFFF;
            cr_[27] = (val >> 16) & 0xFFFF;
            break;
        case 28:
            cr_[28] = val & 0xFFFF;
            cr_[29] = (val >> 16) & 0xFFFF;
            break;
        case 30:
            cr_[30] = val & 0xFFFF;
            cr_[31] = (val >> 16) & 0xFFFF;
            break;
        default:
            cr_[reg] = val;
            break;
    }
}

// ================================
//  Push SXY
// ================================

void Gte::push_sxy() {
    dr_[15] = dr_[12];
    dr_[12] = dr_[13];
    dr_[13] = dr_[14];
    dr_[14] = dr_[15];
}

// ================================
//  Rotation/Translation helpers
// ================================

// Matrix 0 is the primary rotation matrix (cr[0..3] = R11R12, R13, R21R22, R23, R31R32, R33)
// Actually in the GTE:
// cr[0] = R11 | (R12 << 16)
// cr[1] = R13 (s16)
// cr[2] = R21 | (R22 << 16)
// cr[3] = R23
// cr[4] = R31 | (R32 << 16)
// cr[5] = R33
//
// Translation vector:
// cr[6] = TRX (s16)
// cr[7] = TRY
// cr[8] = TRZ

void Gte::rtps() {
    i64 vx = sign16(dr_[0] & 0xFFFF);
    i64 vy = sign16((dr_[0] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[1] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    if (shift) {
        mac1 >>= shift;
        mac2 >>= shift;
        mac3 >>= shift;
    }

    i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
    i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
    i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

    dr_[9] = static_cast<u32>(static_cast<i32>(ir1));  // IR1
    dr_[10] = static_cast<u32>(static_cast<i32>(ir2)); // IR2
    dr_[11] = static_cast<u32>(static_cast<i32>(ir3)); // IR3

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF); // MAC1
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF); // MAC2
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF); // MAC3

    // SXY0 = SXY1, SXY1 = SXY2, SXY2 = new screen XY
    u32 h = (cr_[29] >> 19) & 0x1F;
    i64 h_div2 = h * 65536 / 2;
    i64 sx = h_div2 + mac1 * 65536 / 2 / mac3;
    i64 sy = h_div2 - mac2 * 65536 / 2 / mac3;

    if (mac3 < 0x00010000 && mac3 > -0x00010000) {
        sx = h_div2;
        sy = -h_div2;
    } else if (mac3 > 0x00010000) {
        sx = h_div2 + (mac1 * 65536 / mac3);
        sy = h_div2 - (mac2 * 65536 / mac3);
    }

    push_sxy();
    dr_[12] = static_cast<u32>(static_cast<i32>(sx) & 0xFFFF) |
              (static_cast<u32>(static_cast<i32>(sy) & 0xFFFF) << 16);

    // OTZ
    i64 otz = mac3;
    if (otz < 0) otz = 0;
    if (otz > 0xFFFF) otz = 0xFFFF;
    dr_[7] = static_cast<u32>(otz);
}

void Gte::rtpt() {
    for (int i = 0; i < 3; i++) {
        int vex = (i == 0) ? 0 : (i == 1) ? 2 : 4;
        int vez = (i == 0) ? 1 : (i == 1) ? 3 : 5;

        i64 vx = sign16(dr_[vex] & 0xFFFF);
        i64 vy = sign16((dr_[vex] >> 16) & 0xFFFF);
        i64 vz = sign16(dr_[vez] & 0xFFFF);

        i64 r11 = sign16(cr_[0] & 0xFFFF);
        i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
        i64 r13 = sign16(cr_[1] & 0xFFFF);
        i64 r21 = sign16(cr_[2] & 0xFFFF);
        i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
        i64 r23 = sign16(cr_[3] & 0xFFFF);
        i64 r31 = sign16(cr_[4] & 0xFFFF);
        i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
        i64 r33 = sign16(cr_[5] & 0xFFFF);

        i64 trx = sign16(cr_[6] & 0xFFFF);
        i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
        i64 trz = sign16(cr_[8] & 0xFFFF);

        i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
        i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
        i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

        int shift = (cr_[29] >> 10) & 0x1F;
        if (shift) {
            mac1 >>= shift;
            mac2 >>= shift;
            mac3 >>= shift;
        }

        i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
        i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
        i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

        if (i == 2) {
            dr_[9] = static_cast<u32>(static_cast<i32>(ir1));
            dr_[10] = static_cast<u32>(static_cast<i32>(ir2));
            dr_[11] = static_cast<u32>(static_cast<i32>(ir3));

            cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
            cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
            cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
        }

        u32 h = (cr_[29] >> 19) & 0x1F;
        i64 h_div2 = h * 65536 / 2;
        i64 sx, sy;

        if (mac3 < 0x00010000 && mac3 > -0x00010000) {
            sx = h_div2;
            sy = -h_div2;
        } else if (mac3 > 0x00010000) {
            sx = h_div2 + (mac1 * 65536 / mac3);
            sy = h_div2 - (mac2 * 65536 / mac3);
        } else {
            sx = h_div2 + mac1 * 65536 / 2 / mac3;
            sy = h_div2 - mac2 * 65536 / 2 / mac3;
        }

        u32 sxy = static_cast<u32>(static_cast<i32>(sx) & 0xFFFF) |
                  (static_cast<u32>(static_cast<i32>(sy) & 0xFFFF) << 16);

        if (i == 0) {
            push_sxy();
            dr_[12] = sxy;
        } else if (i == 1) {
            push_sxy();
            dr_[13] = sxy;
        } else {
            push_sxy();
            dr_[14] = sxy;
        }
    }

    i64 otz = (static_cast<i32>(dr_[16] & 0xFFFF) +
               static_cast<i32>(dr_[17] & 0xFFFF) +
               static_cast<i32>(dr_[18] & 0xFFFF)) / 3;
    if (otz < 0) otz = 0;
    if (otz > 0xFFFF) otz = 0xFFFF;
    dr_[7] = static_cast<u32>(otz);
}

void Gte::mvmva(u32 opcode) {
    u32 mx = (opcode >> 17) & 3;
    u32 v = (opcode >> 13) & 3;
    u32 tv = (opcode >> 15) & 3;
    int shift = (cr_[29] >> 10) & 0x1F;

    i64 vx, vy, vz;
    if (v == 0) {
        vx = sign16(dr_[0] & 0xFFFF);
        vy = sign16((dr_[0] >> 16) & 0xFFFF);
        vz = sign16(dr_[1] & 0xFFFF);
    } else if (v == 1) {
        vx = sign16(dr_[2] & 0xFFFF);
        vy = sign16((dr_[2] >> 16) & 0xFFFF);
        vz = sign16(dr_[3] & 0xFFFF);
    } else if (v == 2) {
        vx = sign16(dr_[4] & 0xFFFF);
        vy = sign16((dr_[4] >> 16) & 0xFFFF);
        vz = sign16(dr_[5] & 0xFFFF);
    } else {
        vx = sign16(dr_[9] & 0xFFFF);
        vy = sign16(dr_[10] & 0xFFFF);
        vz = sign16(dr_[11] & 0xFFFF);
    }

    i64 r11, r12, r13, r21, r22, r23, r31, r32, r33;

    if (mx == 0) {
        r11 = sign16(cr_[0] & 0xFFFF);
        r12 = sign16((cr_[0] >> 16) & 0xFFFF);
        r13 = sign16(cr_[1] & 0xFFFF);
        r21 = sign16(cr_[2] & 0xFFFF);
        r22 = sign16((cr_[2] >> 16) & 0xFFFF);
        r23 = sign16(cr_[3] & 0xFFFF);
        r31 = sign16(cr_[4] & 0xFFFF);
        r32 = sign16((cr_[4] >> 16) & 0xFFFF);
        r33 = sign16(cr_[5] & 0xFFFF);
    } else if (mx == 1) {
        r11 = sign16(cr_[10] & 0xFFFF);
        r12 = sign16((cr_[10] >> 16) & 0xFFFF);
        r13 = sign16(cr_[11] & 0xFFFF);
        r21 = sign16(cr_[12] & 0xFFFF);
        r22 = sign16((cr_[12] >> 16) & 0xFFFF);
        r23 = sign16(cr_[13] & 0xFFFF);
        r31 = sign16(cr_[14] & 0xFFFF);
        r32 = sign16((cr_[14] >> 16) & 0xFFFF);
        r33 = sign16(cr_[15] & 0xFFFF);
    } else if (mx == 2) {
        r11 = sign16(cr_[22] & 0xFFFF);
        r12 = sign16((cr_[22] >> 16) & 0xFFFF);
        r13 = sign16(cr_[23] & 0xFFFF);
        r21 = sign16(cr_[24] & 0xFFFF);
        r22 = sign16((cr_[24] >> 16) & 0xFFFF);
        r23 = sign16(cr_[25] & 0xFFFF);
        r31 = sign16(cr_[26] & 0xFFFF);
        r32 = sign16((cr_[26] >> 16) & 0xFFFF);
        r33 = sign16(cr_[27] & 0xFFFF);
    } else {
        r11 = sign16(cr_[30] & 0xFFFF);
        r12 = sign16((cr_[30] >> 16) & 0xFFFF);
        r13 = sign16(cr_[31] & 0xFFFF);
        r21 = r12; r22 = r11; r23 = r13;
        r31 = r12 + r13; r32 = r11 + r13; r33 = r11 + r12;
    }

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz;

    if (shift) {
        mac1 >>= shift;
        mac2 >>= shift;
        mac3 >>= shift;
    }

    i64 tx = 0, ty = 0, tz = 0;
    if (tv == 0) {
        tx = sign16(cr_[6] & 0xFFFF);
        ty = sign16((cr_[6] >> 16) & 0xFFFF);
        tz = sign16(cr_[8] & 0xFFFF);
    } else if (tv == 1) {
        tx = sign16(cr_[7] & 0xFFFF);
        ty = sign16((cr_[7] >> 16) & 0xFFFF);
        tz = sign16(cr_[9] & 0xFFFF);
    }

    mac1 += tx;
    mac2 += ty;
    mac3 += tz;

    dr_[9] = sat(mac1, 32767);
    dr_[10] = sat(mac2, 32767);
    dr_[11] = sat(mac3, 32767);

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
}

void Gte::nclip() {
    i64 x0 = static_cast<i16>(dr_[12] & 0xFFFF);
    i64 y0 = static_cast<i16>((dr_[12] >> 16) & 0xFFFF);
    i64 x1 = static_cast<i16>(dr_[13] & 0xFFFF);
    i64 y1 = static_cast<i16>((dr_[13] >> 16) & 0xFFFF);
    i64 x2 = static_cast<i16>(dr_[14] & 0xFFFF);
    i64 y2 = static_cast<i16>((dr_[14] >> 16) & 0xFFFF);

    i64 result = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    cr_[16] = static_cast<u32>(result & 0xFFFFFFFF);
}

void Gte::avsz3() {
    i64 z0 = static_cast<i16>(dr_[16] & 0xFFFF);
    i64 z1 = static_cast<i16>(dr_[17] & 0xFFFF);
    i64 z2 = static_cast<i16>(dr_[18] & 0xFFFF);
    i64 sum = z0 + z1 + z2;
    cr_[16] = static_cast<u32>(sum & 0xFFFFFFFF);

    i64 otz = sum / 3;
    if (otz < 0) otz = 0;
    if (otz > 0xFFFF) otz = 0xFFFF;
    dr_[7] = static_cast<u32>(otz);
}

void Gte::avsz4() {
    i64 z0 = static_cast<i16>(dr_[16] & 0xFFFF);
    i64 z1 = static_cast<i16>(dr_[17] & 0xFFFF);
    i64 z2 = static_cast<i16>(dr_[18] & 0xFFFF);
    i64 z3 = static_cast<i16>(dr_[19] & 0xFFFF);
    i64 sum = z0 + z1 + z2 + z3;
    cr_[16] = static_cast<u32>(sum & 0xFFFFFFFF);

    i64 otz = sum / 4;
    if (otz < 0) otz = 0;
    if (otz > 0xFFFF) otz = 0xFFFF;
    dr_[7] = static_cast<u32>(otz);
}

void Gte::dpcs() {
    u32 r = dr_[6] & 0x1F;
    [[maybe_unused]] u32 g = (dr_[6] >> 5) & 0x1F;
    [[maybe_unused]] u32 b = (dr_[6] >> 10) & 0x1F;
    u32 code = (dr_[6] >> 16) & 0xFF;

    i64 ir1 = static_cast<i32>(dr_[9]);
    i64 ir2 = static_cast<i32>(dr_[10]);
    i64 ir3 = static_cast<i32>(dr_[11]);

    i64 mac1 = ir1 * code / 256;
    i64 mac2 = ir2 * code / 256;
    i64 mac3 = ir3 * code / 256;

    dr_[9] = sat(mac1, 32767);
    dr_[10] = sat(mac2, 32767);
    dr_[11] = sat(mac3, 32767);

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);

    i64 r2 = mac1 * r / 256;
    i64 g2 = mac2 * r / 256;
    i64 b2 = mac3 * r / 256;

    dr_[20] = lim(r2, 0x1F) | (lim(g2, 0x1F) << 5) | (lim(b2, 0x1F) << 10) |
              ((cr_[16] >> 16) & 0xFF) << 16;
}

void Gte::dpct() {
    for (int i = 0; i < 3; i++) {
        u32 rgb_reg = (i == 0) ? 20 : (i == 1) ? 21 : 22;
        u32 r = dr_[rgb_reg] & 0x1F;
        [[maybe_unused]] u32 g = (dr_[rgb_reg] >> 5) & 0x1F;
        [[maybe_unused]] u32 b = (dr_[rgb_reg] >> 10) & 0x1F;
        u32 code = (dr_[rgb_reg] >> 16) & 0xFF;

        i64 ir1 = static_cast<i32>(dr_[9]);
        i64 ir2 = static_cast<i32>(dr_[10]);
        i64 ir3 = static_cast<i32>(dr_[11]);

        i64 mac1 = ir1 * code / 256;
        i64 mac2 = ir2 * code / 256;
        i64 mac3 = ir3 * code / 256;

        if (i == 2) {
            dr_[9] = sat(mac1, 32767);
            dr_[10] = sat(mac2, 32767);
            dr_[11] = sat(mac3, 32767);

            cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
            cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
            cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
        }

        i64 r2 = mac1 * r / 256;
        i64 g2 = mac2 * r / 256;
        i64 b2 = mac3 * r / 256;

        dr_[rgb_reg] = lim(r2, 0x1F) | (lim(g2, 0x1F) << 5) | (lim(b2, 0x1F) << 10) |
                       ((cr_[16] >> 16) & 0xFF) << 16;
    }
}

void Gte::ncds() {
    i64 vx = sign16(dr_[0] & 0xFFFF);
    i64 vy = sign16((dr_[0] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[1] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    if (shift) {
        mac1 >>= shift;
        mac2 >>= shift;
        mac3 >>= shift;
    }

    i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
    i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
    i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

    dr_[9] = static_cast<u32>(static_cast<i32>(ir1));
    dr_[10] = static_cast<u32>(static_cast<i32>(ir2));
    dr_[11] = static_cast<u32>(static_cast<i32>(ir3));

    // Normal clip
    i64 x0 = static_cast<i16>(dr_[12] & 0xFFFF);
    i64 y0 = static_cast<i16>((dr_[12] >> 16) & 0xFFFF);
    i64 x1 = static_cast<i16>(dr_[13] & 0xFFFF);
    i64 y1 = static_cast<i16>((dr_[13] >> 16) & 0xFFFF);
    i64 x2 = static_cast<i16>(dr_[14] & 0xFFFF);
    i64 y2 = static_cast<i16>((dr_[14] >> 16) & 0xFFFF);
    [[maybe_unused]] i64 nclip = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);

    u32 r = dr_[6] & 0x1F;
    [[maybe_unused]] u32 g = (dr_[6] >> 5) & 0x1F;
    [[maybe_unused]] u32 b = (dr_[6] >> 10) & 0x1F;
    u32 code = (dr_[6] >> 16) & 0xFF;

    i64 lm1 = ir1 * code / 256;
    i64 lm2 = ir2 * code / 256;
    i64 lm3 = ir3 * code / 256;

    i64 lr = lm1 * r / 256;
    i64 lg = lm2 * r / 256;
    i64 lb = lm3 * r / 256;

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);

    dr_[20] = lim(lr, 0x1F) | (lim(lg, 0x1F) << 5) | (lim(lb, 0x1F) << 10) |
              ((cr_[16] >> 16) & 0xFF) << 16;
}

void Gte::ncdt() {
    for (int i = 0; i < 3; i++) {
        int vex = (i == 0) ? 0 : (i == 1) ? 2 : 4;
        int vez = (i == 0) ? 1 : (i == 1) ? 3 : 5;

        i64 vx = sign16(dr_[vex] & 0xFFFF);
        i64 vy = sign16((dr_[vex] >> 16) & 0xFFFF);
        i64 vz = sign16(dr_[vez] & 0xFFFF);

        i64 r11 = sign16(cr_[0] & 0xFFFF);
        i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
        i64 r13 = sign16(cr_[1] & 0xFFFF);
        i64 r21 = sign16(cr_[2] & 0xFFFF);
        i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
        i64 r23 = sign16(cr_[3] & 0xFFFF);
        i64 r31 = sign16(cr_[4] & 0xFFFF);
        i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
        i64 r33 = sign16(cr_[5] & 0xFFFF);

        i64 trx = sign16(cr_[6] & 0xFFFF);
        i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
        i64 trz = sign16(cr_[8] & 0xFFFF);

        i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
        i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
        i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

        int shift = (cr_[29] >> 10) & 0x1F;
        if (shift) {
            mac1 >>= shift;
            mac2 >>= shift;
            mac3 >>= shift;
        }

        i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
        i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
        i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

        if (i == 2) {
            dr_[9] = static_cast<u32>(static_cast<i32>(ir1));
            dr_[10] = static_cast<u32>(static_cast<i32>(ir2));
            dr_[11] = static_cast<u32>(static_cast<i32>(ir3));
        }

        u32 rgb_reg = (i == 0) ? 20 : (i == 1) ? 21 : 22;
        u32 rv = dr_[rgb_reg] & 0x1F;
        u32 gv = (dr_[rgb_reg] >> 5) & 0x1F;
        u32 bv = (dr_[rgb_reg] >> 10) & 0x1F;
        u32 code = (dr_[rgb_reg] >> 16) & 0xFF;

        i64 lm1 = ir1 * code / 256;
        i64 lm2 = ir2 * code / 256;
        i64 lm3 = ir3 * code / 256;
        i64 lr = lm1 * rv / 256;
        i64 lg = lm2 * gv / 256;
        i64 lb = lm3 * bv / 256;

        if (i == 2) {
            cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
            cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
            cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
        }

        dr_[rgb_reg] = lim(lr, 0x1F) | (lim(lg, 0x1F) << 5) | (lim(lb, 0x1F) << 10) |
                       ((cr_[16] >> 16) & 0xFF) << 16;
    }
}

void Gte::ncss() {
    i64 vx = sign16(dr_[0] & 0xFFFF);
    i64 vy = sign16((dr_[0] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[1] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    if (shift) {
        mac1 >>= shift;
        mac2 >>= shift;
        mac3 >>= shift;
    }

    i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
    i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
    i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

    dr_[9] = static_cast<u32>(static_cast<i32>(ir1));
    dr_[10] = static_cast<u32>(static_cast<i32>(ir2));
    dr_[11] = static_cast<u32>(static_cast<i32>(ir3));

    [[maybe_unused]] u32 r = dr_[6] & 0x1F;
    [[maybe_unused]] u32 g = (dr_[6] >> 5) & 0x1F;
    [[maybe_unused]] u32 b = (dr_[6] >> 10) & 0x1F;
    u32 code = (dr_[6] >> 16) & 0xFF;

    i64 lm1 = ir1 * code / 256;
    i64 lm2 = ir2 * code / 256;
    i64 lm3 = ir3 * code / 256;

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);

    dr_[20] = lim(lm1, 0x1F) | (lim(lm2, 0x1F) << 5) | (lim(lm3, 0x1F) << 10) |
              ((cr_[16] >> 16) & 0xFF) << 16;
}

void Gte::ncst() {
    for (int i = 0; i < 3; i++) {
        int vex = (i == 0) ? 0 : (i == 1) ? 2 : 4;
        int vez = (i == 0) ? 1 : (i == 1) ? 3 : 5;

        i64 vx = sign16(dr_[vex] & 0xFFFF);
        i64 vy = sign16((dr_[vex] >> 16) & 0xFFFF);
        i64 vz = sign16(dr_[vez] & 0xFFFF);

        i64 r11 = sign16(cr_[0] & 0xFFFF);
        i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
        i64 r13 = sign16(cr_[1] & 0xFFFF);
        i64 r21 = sign16(cr_[2] & 0xFFFF);
        i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
        i64 r23 = sign16(cr_[3] & 0xFFFF);
        i64 r31 = sign16(cr_[4] & 0xFFFF);
        i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
        i64 r33 = sign16(cr_[5] & 0xFFFF);

        i64 trx = sign16(cr_[6] & 0xFFFF);
        i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
        i64 trz = sign16(cr_[8] & 0xFFFF);

        i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
        i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
        i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;
        int shift = (cr_[29] >> 10) & 0x1F;
        if (shift) { mac1 >>= shift; mac2 >>= shift; mac3 >>= shift; }

        i64 ir1 = std::clamp<i64>(mac1, -32768LL, 32767LL);
        i64 ir2 = std::clamp<i64>(mac2, -32768LL, 32767LL);
        i64 ir3 = std::clamp<i64>(mac3, -32768LL, 32767LL);

        if (i == 2) {
            dr_[9] = static_cast<u32>(static_cast<i32>(ir1));
            dr_[10] = static_cast<u32>(static_cast<i32>(ir2));
            dr_[11] = static_cast<u32>(static_cast<i32>(ir3));
        }

        u32 rgb_reg = (i == 0) ? 20 : (i == 1) ? 21 : 22;
        u32 code = (dr_[rgb_reg] >> 16) & 0xFF;

        i64 lm1 = ir1 * code / 256;
        i64 lm2 = ir2 * code / 256;
        i64 lm3 = ir3 * code / 256;

        if (i == 2) {
            cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
            cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
            cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
        }

        dr_[rgb_reg] = lim(lm1, 0x1F) | (lim(lm2, 0x1F) << 5) | (lim(lm3, 0x1F) << 10) |
                       ((cr_[16] >> 16) & 0xFF) << 16;
    }
}

void Gte::op_sqr(u32 opcode) {
    int shift = (cr_[29] >> 10) & 0x1F;
    u32 v = (opcode >> 13) & 3;
    i64 val;
    if (v == 0) val = sign16(dr_[0] & 0xFFFF);
    else if (v == 1) val = sign16(dr_[2] & 0xFFFF);
    else if (v == 2) val = sign16(dr_[4] & 0xFFFF);
    else val = static_cast<i16>(dr_[9] & 0xFFFF);

    i64 result = val * val;
    if (shift) result >>= shift;

    cr_[16] = static_cast<u32>(result & 0xFFFFFFFF);
    dr_[9] = sat(result, 32767);
}

void Gte::op_dcpl(u32 opcode) {
    u32 rgb_reg = (opcode >> 13) & 3;
    u32 regs[] = {20, 21, 22};
    u32 rv = dr_[regs[rgb_reg]] & 0x1F;
    u32 gv = (dr_[regs[rgb_reg]] >> 5) & 0x1F;
    u32 bv = (dr_[regs[rgb_reg]] >> 10) & 0x1F;
    u32 code = (dr_[regs[rgb_reg]] >> 16) & 0xFF;

    i64 ir1 = static_cast<i32>(dr_[9]);
    i64 ir2 = static_cast<i32>(dr_[10]);
    i64 ir3 = static_cast<i32>(dr_[11]);

    i64 mac1 = ir1 * code / 256;
    i64 mac2 = ir2 * code / 256;
    i64 mac3 = ir3 * code / 256;

    i64 lr = mac1 * rv / 256;
    i64 lg = mac2 * gv / 256;
    i64 lb = mac3 * bv / 256;

    dr_[regs[rgb_reg]] = lim(lr, 0x1F) | (lim(lg, 0x1F) << 5) | (lim(lb, 0x1F) << 10) |
                         ((cr_[16] >> 16) & 0xFF) << 16;
}

void Gte::gpf(u32 opcode) {
    int shift = (cr_[29] >> 10) & 0x1F;
    u32 v = (opcode >> 13) & 3;
    i64 v0_val, v1_val;

    if (v == 0) {
        v0_val = sign16(dr_[0] & 0xFFFF);
        v1_val = sign16((dr_[0] >> 16) & 0xFFFF);
        i64 result = v0_val * v1_val;
        if (shift) result >>= shift;
        dr_[9] = sat(result, 32767);
        cr_[16] = static_cast<u32>(result & 0xFFFFFFFF);
    }
}

void Gte::gpl(u32 opcode) {
    int shift = (cr_[29] >> 10) & 0x1F;
    u32 v = (opcode >> 13) & 3;
    i64 val;
    if (v == 0) val = sign16(dr_[0] & 0xFFFF);
    else if (v == 1) val = sign16(dr_[2] & 0xFFFF);
    else if (v == 2) val = sign16(dr_[4] & 0xFFFF);
    else val = static_cast<i16>(dr_[9] & 0xFFFF);

    i64 mac = static_cast<i32>(cr_[16]);
    i64 result = mac + val * val;
    if (shift) result >>= shift;

    dr_[9] = sat(result, 32767);
    cr_[16] = static_cast<u32>(result & 0xFFFFFFFF);
}

void Gte::op_intpl() {
    i64 ir1 = static_cast<i32>(dr_[9]);
    i64 ir2 = static_cast<i32>(dr_[10]);
    i64 ir3 = static_cast<i32>(dr_[11]);

    i64 mac1 = (cr_[16] & 0xFFFFFFFF);
    i64 mac2 = (cr_[17] & 0xFFFFFFFF);
    i64 mac3 = (cr_[18] & 0xFFFFFFFF);

    i64 r = ir1 + (mac1 - ir1) * (dr_[6] & 0x1F) / 32;
    i64 g = ir2 + (mac2 - ir2) * ((dr_[6] >> 5) & 0x1F) / 32;
    i64 b = ir3 + (mac3 - ir3) * ((dr_[6] >> 10) & 0x1F) / 32;

    dr_[9] = sat(r, 32767);
    dr_[10] = sat(g, 32767);
    dr_[11] = sat(b, 32767);
}

void Gte::op_cc(u32 opcode) {
    [[maybe_unused]] u32 rgb_reg = (opcode >> 13) & 3;
    u32 regs[] = {20, 21, 22};

    for (int i = 0; i < 3; i++) {
        u32 cr_val = dr_[regs[i]];
        [[maybe_unused]] u32 rv = cr_val & 0x1F;
        [[maybe_unused]] u32 gv = (cr_val >> 5) & 0x1F;
        [[maybe_unused]] u32 bv = (cr_val >> 10) & 0x1F;
        u32 code = (cr_val >> 16) & 0xFF;

        i64 ir1 = static_cast<i32>(dr_[9]);
        i64 ir2 = static_cast<i32>(dr_[10]);
        i64 ir3 = static_cast<i32>(dr_[11]);

        i64 mac1 = ir1 * code / 256;
        i64 mac2 = ir2 * code / 256;
        i64 mac3 = ir3 * code / 256;

        if (i == 2) {
            dr_[9] = sat(mac1, 32767);
            dr_[10] = sat(mac2, 32767);
            dr_[11] = sat(mac3, 32767);
            cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
            cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
            cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
        }
    }
}

void Gte::rtv0(u32 /*opcode*/) {
    i64 vx = sign16(dr_[0] & 0xFFFF);
    i64 vy = sign16((dr_[0] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[1] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    int lm = (cr_[29] >> 1) & 1;
    if (shift) {
        mac1 >>= shift; mac2 >>= shift; mac3 >>= shift;
    }

    if (lm) {
        dr_[9] = sat(mac1, 32767);
        dr_[10] = sat(mac2, 32767);
        dr_[11] = sat(mac3, 32767);
    } else {
        dr_[9] = std::clamp<i64>(mac1, -32768LL, 32767LL);
        dr_[10] = std::clamp<i64>(mac2, -32768LL, 32767LL);
        dr_[11] = std::clamp<i64>(mac3, -32768LL, 32767LL);
    }

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
}

void Gte::rtv1(u32 /*opcode*/) {
    i64 vx = sign16(dr_[2] & 0xFFFF);
    i64 vy = sign16((dr_[2] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[3] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    int lm = (cr_[29] >> 1) & 1;
    if (shift) {
        mac1 >>= shift; mac2 >>= shift; mac3 >>= shift;
    }

    if (lm) {
        dr_[9] = sat(mac1, 32767);
        dr_[10] = sat(mac2, 32767);
        dr_[11] = sat(mac3, 32767);
    } else {
        dr_[9] = std::clamp<i64>(mac1, -32768LL, 32767LL);
        dr_[10] = std::clamp<i64>(mac2, -32768LL, 32767LL);
        dr_[11] = std::clamp<i64>(mac3, -32768LL, 32767LL);
    }

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
}

void Gte::rtv2(u32 /*opcode*/) {
    i64 vx = sign16(dr_[4] & 0xFFFF);
    i64 vy = sign16((dr_[4] >> 16) & 0xFFFF);
    i64 vz = sign16(dr_[5] & 0xFFFF);

    i64 r11 = sign16(cr_[0] & 0xFFFF);
    i64 r12 = sign16((cr_[0] >> 16) & 0xFFFF);
    i64 r13 = sign16(cr_[1] & 0xFFFF);
    i64 r21 = sign16(cr_[2] & 0xFFFF);
    i64 r22 = sign16((cr_[2] >> 16) & 0xFFFF);
    i64 r23 = sign16(cr_[3] & 0xFFFF);
    i64 r31 = sign16(cr_[4] & 0xFFFF);
    i64 r32 = sign16((cr_[4] >> 16) & 0xFFFF);
    i64 r33 = sign16(cr_[5] & 0xFFFF);

    i64 trx = sign16(cr_[6] & 0xFFFF);
    i64 try_ = sign16((cr_[6] >> 16) & 0xFFFF);
    i64 trz = sign16(cr_[8] & 0xFFFF);

    i64 mac1 = r11 * vx + r12 * vy + r13 * vz + trx;
    i64 mac2 = r21 * vx + r22 * vy + r23 * vz + try_;
    i64 mac3 = r31 * vx + r32 * vy + r33 * vz + trz;

    int shift = (cr_[29] >> 10) & 0x1F;
    int lm = (cr_[29] >> 1) & 1;
    if (shift) {
        mac1 >>= shift; mac2 >>= shift; mac3 >>= shift;
    }

    if (lm) {
        dr_[9] = sat(mac1, 32767);
        dr_[10] = sat(mac2, 32767);
        dr_[11] = sat(mac3, 32767);
    } else {
        dr_[9] = std::clamp<i64>(mac1, -32768LL, 32767LL);
        dr_[10] = std::clamp<i64>(mac2, -32768LL, 32767LL);
        dr_[11] = std::clamp<i64>(mac3, -32768LL, 32767LL);
    }

    cr_[16] = static_cast<u32>(mac1 & 0xFFFFFFFF);
    cr_[17] = static_cast<u32>(mac2 & 0xFFFFFFFF);
    cr_[18] = static_cast<u32>(mac3 & 0xFFFFFFFF);
}

void Gte::execute(u32 opcode) {
    opcode_ = opcode;
    cmd_ = (opcode >> 25) & 3;
    funct_ = opcode & 0x3F;

    if (cmd_ != 2) return;

    switch (funct_) {
        case 0x01: case 0x30: rtps(); break;
        case 0x06: nclip(); break;
        case 0x0C: op_sqr(opcode); break;
        case 0x10: mvmva(opcode); break;
        case 0x11: case 0x1B: case 0x20: ncds(); break;
        case 0x13: case 0x1C: case 0x28: ncdt(); break;
        case 0x14: op_intpl(); break;
        case 0x16: gpf(opcode); break;
        case 0x18: gpl(opcode); break;
        case 0x19: case 0x1D: ncss(); break;
        case 0x1A: case 0x1E: ncst(); break;
        case 0x2D: case 0x3D: avsz3(); break;
        case 0x2E: case 0x3E: avsz4(); break;
        case 0x31: rtpt(); break;
        default:
            LOG_DEBUG("Unhandled GTE funct {:02X}", funct_);
            break;
    }
}

} // namespace yaps1
