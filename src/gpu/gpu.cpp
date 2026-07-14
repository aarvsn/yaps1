/// @file gpu.cpp
/// @brief GPU implementation — software renderer with full GP0/GP1 commands.

#include "gpu/gpu.h"
#include "common/memory_map.h"
#include "common/logger.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace yaps1 {

// ===========================================================================
//  GP0 command lengths (in 32-bit words)
// ===========================================================================

/// Returns the expected number of 32-bit words for a GP0 command.
static int gp0_cmd_length(u32 opcode) {
    u32 cmd = (opcode >> 24) & 0xFF;
    switch (cmd) {
        case 0x00: return 1;  // NOP
        case 0x01: return 1;  // Clear cache
        case 0x02: return 3;  // Fill rect
        case 0x03: return 3;  // NOP (copy rect)
        // Polygon primitives: 1=shaded, 4=textured, 8=raw-texture, etc.
        case 0x20: case 0x21: case 0x22: case 0x23: return 4;  // Tri non-textured
        case 0x24: case 0x25: case 0x26: case 0x27: return 5;  // Tri textured
        case 0x28: case 0x29: case 0x2A: case 0x2B: return 6;  // Quad non-textured
        case 0x2C: case 0x2D: case 0x2E: case 0x2F: return 9;  // Quad textured
        // Line primitives
        case 0x40: case 0x41: case 0x42: case 0x43: return 4;  // 2-point line
        case 0x48: case 0x49: case 0x4A: case 0x4B: return 3;  // 1-point line (poly-line)
        case 0x4C: case 0x4D: case 0x4E: case 0x4F: return 4;
        case 0x50: case 0x51: case 0x52: case 0x53: return 5;
        case 0x54: case 0x55: case 0x56: case 0x57: return 6;
        case 0x58: case 0x59: case 0x5A: case 0x5B: return 7;
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: return 8;
        // Rectangle primitives
        case 0x60: case 0x61: case 0x62: case 0x63: return 3;  // Variable-size rect
        case 0x64: case 0x65: case 0x66: case 0x67: return 4;  // 1x1 textured
        case 0x68: case 0x69: case 0x6A: case 0x6B: return 4;  // 8x8 textured
        case 0x6C: case 0x6D: case 0x6E: case 0x6F: return 4;  // 16x16 textured
        case 0x70: case 0x71: case 0x72: case 0x73: return 4;  // Variable-size textured
        // VRAM operations
        case 0x80: case 0xA0: case 0xC0: return 3;  // VRAM transfer headers
        // Image load (continued by DMA)
        case 0xE1: return 2;  // Texture page setting
        case 0xE2: return 2;  // Texture window setting
        case 0xE3: return 2;  // Drawing area start
        case 0xE4: return 2;  // Drawing area end
        case 0xE5: return 2;  // Drawing offset
        case 0xE6: return 1;  // Sticky bit
        default:   return 1;
    }
}

// ===========================================================================
//  Constructor / Reset
// ===========================================================================

Gpu::Gpu() { reset(); }

void Gpu::reset() {
    vram_.fill(0);
    cmd_buffer_.clear();
    cmd_len_ = 0;
    cmd_words_ = 0;
    stat_ = 0x14802000;  // After GPU reset
    tex_page_x_ = 0; tex_page_y_ = 0;
    tex_trans_mode_ = 0;
    tex_win_x_ = 0; tex_win_y_ = 0; tex_win_w_ = 256; tex_win_h_ = 256;
    draw_area_left_ = 0; draw_area_top_ = 0;
    draw_area_right_ = 255; draw_area_bottom_ = 239;
    draw_offset_x_ = 0; draw_offset_y_ = 0;
    dither_ = true;
    dot_clocks_ = 0; hclock_ = 0; vline_ = 0;
    in_vblank_ = false; frame_ready_ = false; irq_pending_ = false;
}

// ===========================================================================
//  Timing
// ===========================================================================

void Gpu::tick(u32 dot_clocks) {
    dot_clocks_ += dot_clocks;
    // PS1 GPU runs at ~33.8688 MHz (system clock).
    // Horizontal line: 3406 dot clocks
    // Vertical: 263 lines (NTSC) = ~896K dot clocks per frame.
    // We approximate.

    // Simple frame timing: count dots and trigger VBlank periodically.
    // Full frame = 263 * 3406 = ~896,778 dot clocks (NTSC).
    // VBlank starts at line 240 and lasts 23 lines.
    static constexpr u32 DOTS_PER_LINE = 3406;
    static constexpr u32 LINES_PER_FRAME = 263;
    static constexpr u32 VBLANK_START_LINE = 240;

    while (dot_clocks_ >= DOTS_PER_LINE) {
        dot_clocks_ -= DOTS_PER_LINE;
        vline_++;

        if (vline_ == VBLANK_START_LINE) {
            in_vblank_ = true;
            frame_ready_ = true;
            // Set VBlank bit in STAT
            stat_ |= (1u << 31);
            // Trigger IRQ on VBlank start if enabled (stat bit 4)
            if (stat_ & (1u << 4)) {
                irq_pending_ = true;
            }
        }

        if (vline_ >= LINES_PER_FRAME) {
            vline_ = 0;
            in_vblank_ = false;
            stat_ &= ~(1u << 31);
        }
    }

    // Update horizontal position in STAT (bits 14:8)
    stat_ = (stat_ & ~(0x7F00u)) | ((hclock_ & 0xFF) << 8);
    hclock_ = (hclock_ + 1) & 0xFF;
}

// ===========================================================================
//  GP0 write
// ===========================================================================

void Gpu::write_gp0(u32 val) {
    // If we're in a VRAM write transfer (command 0x80), just pump data.
    if (!cmd_buffer_.empty() && cmd_buffer_[0] >> 24 == 0x80) {
        // VRAM write: we receive pixel data after the 3-word header.
        if (cmd_words_ >= 3) {
            // Each 32-bit word = 2 pixels (16-bit each).
            u16 lo = static_cast<u16>(val & 0xFFFF);
            u16 hi = static_cast<u16>((val >> 16) & 0xFFFF);

            if (vram_write_remaining_ > 0) {
                vram_write(vram_write_x_, vram_write_y_, lo);
                vram_write_x_++;
                if (vram_write_x_ >= vram_write_end_x_) {
                    vram_write_x_ = vram_write_start_x_;
                    vram_write_y_++;
                }
                vram_write_remaining_--;
            }
            if (vram_write_remaining_ > 0) {
                vram_write(vram_write_x_, vram_write_y_, hi);
                vram_write_x_++;
                if (vram_write_x_ >= vram_write_end_x_) {
                    vram_write_x_ = vram_write_start_x_;
                    vram_write_y_++;
                }
                vram_write_remaining_--;
            }
        }
        cmd_words_++;
        return;
    }

    cmd_buffer_.push_back(val);
    cmd_words_++;

    if (cmd_words_ == 1) {
        // First word — determine command length.
        cmd_len_ = gp0_cmd_length(val);
    }

    if (cmd_words_ >= cmd_len_) {
        process_command();
        cmd_buffer_.clear();
        cmd_words_ = 0;
        cmd_len_ = 0;
    }
}

u32 Gpu::read_gp0() {
    // GP0 read returns VRAM data during a VRAM read transfer.
    if (vram_read_remaining_ > 0) {
        u16 lo = vram_read(vram_read_x_, vram_read_y_);
        vram_read_x_++;
        if (vram_read_x_ >= vram_read_end_x_) {
            vram_read_x_ = vram_read_start_x_;
            vram_read_y_++;
        }
        vram_read_remaining_--;

        u16 hi = 0;
        if (vram_read_remaining_ > 0) {
            hi = vram_read(vram_read_x_, vram_read_y_);
            vram_read_x_++;
            if (vram_read_x_ >= vram_read_end_x_) {
                vram_read_x_ = vram_read_start_x_;
                vram_read_y_++;
            }
            vram_read_remaining_--;
        }
        return (static_cast<u32>(hi) << 16) | static_cast<u32>(lo);
    }
    return 0;
}

// ===========================================================================
//  GP1 write
// ===========================================================================

void Gpu::write_gp1(u32 val) {
    u32 cmd = (val >> 24) & 0xFF;
    switch (cmd) {
        case 0x00: gp1_reset_cmd();   break;
        case 0x01: gp1_reset_int();   break;
        case 0x02:                     // Acknowledge IRQ
            irq_pending_ = false;
            break;
        case 0x03: gp1_display_enable(val); break;
        case 0x04: gp1_dma_dir(val);   break;
        case 0x05: gp1_display_mode(val); break;
        case 0x06: gp1_display_area(val); break;
        case 0x07: gp1_horiz_display(val); break;
        case 0x08: gp1_vert_display(val); break;
        default:
            LOG_DEBUG("Unknown GP1 command {:02X}", cmd);
            break;
    }
}

u32 Gpu::read_gp1() {
    // GP1(0x10) read — return GPUSTAT.
    u32 s = stat_;
    if (irq_pending_) s |= (1u << 24);
    return s;
}

// ===========================================================================
//  GP1 sub-commands
// ===========================================================================

void Gpu::gp1_reset_cmd() {
    // Reset command buffer.
    cmd_buffer_.clear();
    cmd_words_ = 0;
    cmd_len_ = 0;
    // Clear texture window, page, etc.
    tex_page_x_ = 0; tex_page_y_ = 0;
    tex_trans_mode_ = 0;
    tex_win_x_ = 0; tex_win_y_ = 0; tex_win_w_ = 256; tex_win_h_ = 256;
    dither_ = true;
}

void Gpu::gp1_reset_int() {
    irq_pending_ = false;
}

void Gpu::gp1_display_enable(u32 val) {
    bool enable = (val & 1) == 0;  // 0 = enable, 1 = disable (inverted)
    if (enable) stat_ &= ~(1u << 23);
    else       stat_ |=  (1u << 23);
}

void Gpu::gp1_dma_dir(u32 val) {
    // Bits 1:0 of data — DMA direction.
    u32 dir = val & 3;
    stat_ = (stat_ & ~0x06u) | ((dir & 3) << 1);
}

void Gpu::gp1_display_mode(u32 val) {
    // Set display mode bits in STAT[20:16].
    stat_ = (stat_ & ~0x003F0000u) | ((val & 0x3F) << 16);
}

void Gpu::gp1_display_area(u32 val) {
    // Set display area start X,Y.
    // Stored internally for display output (not critical for terminal mode).
    (void)val;
}

void Gpu::gp1_horiz_display(u32 val) {
    // Horizontal display range.
    (void)val;
}

void Gpu::gp1_vert_display(u32 val) {
    // Vertical display range (line start, line end).
    (void)val;
}

// ===========================================================================
//  VRAM helpers
// ===========================================================================

u16 Gpu::vram_read(u32 x, u32 y) const {
    x &= VRAM_WIDTH - 1;
    y &= VRAM_HEIGHT - 1;
    return vram_[y * VRAM_WIDTH + x];
}

void Gpu::vram_write(u32 x, u32 y, u16 color) {
    x &= VRAM_WIDTH - 1;
    y &= VRAM_HEIGHT - 1;
    vram_[y * VRAM_WIDTH + x] = color;
}

void Gpu::vram_read_rect(u32 x, u32 y, u32 w, u32 h, u16* dst) {
    for (u32 row = 0; row < h; row++) {
        for (u32 col = 0; col < w; col++) {
            dst[row * w + col] = vram_read(x + col, y + row);
        }
    }
}

void Gpu::vram_write_rect(u32 x, u32 y, u32 w, u32 h, const u16* src) {
    for (u32 row = 0; row < h; row++) {
        for (u32 col = 0; col < w; col++) {
            vram_write(x + col, y + row, src[row * w + col]);
        }
    }
}

// ===========================================================================
//  Texture sampling
// ===========================================================================

u16 Gpu::sample_texture(u8 u, u8 v) {
    // Apply texture window
    u32 tw_x = tex_win_x_ & 0xF;
    u32 tw_y = tex_win_y_ & 0xF;
    u32 tw_w = tex_win_w_ >> 3;
    u32 tw_h = tex_win_h_ >> 3;
    if (tw_w < 1) tw_w = 1;
    if (tw_h < 1) tw_h = 1;

    u8 fu = static_cast<u8>(((u + tw_x) % tw_w) + (tex_win_x_ & ~0xF));
    u8 fv = static_cast<u8>(((v + tw_y) % tw_h) + (tex_win_y_ & ~0xF));

    // Look up in VRAM at texture page
    u32 tx = tex_page_x_ + fu;
    u32 ty = tex_page_y_ + fv;
    return vram_read(tx, ty);
}

// ===========================================================================
//  Clipping
// ===========================================================================

bool Gpu::clip_to_area(i32& x, i32& y) const {
    if (x < static_cast<i32>(draw_area_left_) ||
        x > static_cast<i32>(draw_area_right_) ||
        y < static_cast<i32>(draw_area_top_) ||
        y > static_cast<i32>(draw_area_bottom_)) {
        return false;
    }
    return true;
}

// ===========================================================================
//  Dithering
// ===========================================================================

// 4x4 dither matrix (PS1 standard)
static constexpr i32 DITHER_MATRIX[4][4] = {
    { -4,  0, -3,  1 },
    {  2, -2,  3, -1 },
    { -3,  1, -4,  0 },
    {  3, -1,  2, -2 },
};

void Gpu::dither(i32 x, i32 y, u32& r, u32& g, u32& b) {
    if (!dither_) return;
    i32 d = DITHER_MATRIX[y & 3][x & 3];
    i32 tr = static_cast<i32>(r) + d;
    i32 tg = static_cast<i32>(g) + d;
    i32 tb = static_cast<i32>(b) + d;
    r = static_cast<u32>(std::clamp(tr, 0, 31));
    g = static_cast<u32>(std::clamp(tg, 0, 31));
    b = static_cast<u32>(std::clamp(tb, 0, 31));
}

// ===========================================================================
//  Fill rectangle
// ===========================================================================

void Gpu::fill_rect(u32 x, u32 y, u32 w, u32 h, u16 color) {
    for (u32 row = 0; row < h; row++) {
        for (u32 col = 0; col < w; col++) {
            vram_write(x + col, y + row, color);
        }
    }
}

// ===========================================================================
//  Draw polygon (triangle / quad)
// ===========================================================================

void Gpu::draw_poly_triangle(bool textured, bool shaded, u32* params) {
    // params layout:
    //   Non-textured, flat:  [color, x0,y0, x1,y1, x2,y2]  (4 words)
    //   Non-textured, gouraud: [color0,x0,y0, color1,x1,y1, color2,x2,y2] (6 words)
    //   Textured, flat: [color, x0,y0,uv0, x1,y1,uv1, x2,y2,uv2, clut] (5+ words)
    //   Textured, gouraud: [color0,x0,y0,uv0, color1,..., color2,..., clut] (9 words)

    // Unshaded triangle: vertices from params
    u32 r = (params[0] >> 0) & 0xFF;
    u32 g = (params[0] >> 8) & 0xFF;
    u32 b = (params[0] >> 16) & 0xFF;
    // Scale 8-bit to 5-bit for VRAM
    r = r >> 3; g = g >> 3; b = b >> 3;

    i32 x0 = static_cast<i32>(params[1] & 0xFFFF);
    i32 y0 = static_cast<i32>((params[1] >> 16) & 0xFFFF);
    i32 x1 = static_cast<i32>(params[2] & 0xFFFF);
    i32 y1 = static_cast<i32>((params[2] >> 16) & 0xFFFF);
    i32 x2 = static_cast<i32>(params[3] & 0xFFFF);
    i32 y2 = static_cast<i32>((params[3] >> 16) & 0xFFFF);

    // Apply drawing offset
    x0 += draw_offset_x_; y0 += draw_offset_y_;
    x1 += draw_offset_x_; y1 += draw_offset_y_;
    x2 += draw_offset_x_; y2 += draw_offset_y_;

    // Sign-extend 16-bit coordinates
    auto sext16 = [](i32 v) -> i32 { return (v & 0x8000) ? (v - 0x10000) : v; };
    x0 = sext16(x0); y0 = sext16(y0);
    x1 = sext16(x1); y1 = sext16(y1);
    x2 = sext16(x2); y2 = sext16(y2);

    if (shaded) {
        // Gouraud: color comes from per-vertex colors
        r = ((params[0] >> 0) & 0xFF) >> 3;
        g = ((params[0] >> 8) & 0xFF) >> 3;
        b = ((params[0] >> 16) & 0xFF) >> 3;
    }

    // ---- Bounding box rasterization (scanline) ----
    i32 min_y = std::min({y0, y1, y2});
    i32 max_y = std::max({y0, y1, y2});
    i32 min_x = std::min({x0, x1, x2});
    i32 max_x = std::max({x0, x1, x2});

    // Clip to drawing area
    min_y = std::max(min_y, static_cast<i32>(draw_area_top_));
    max_y = std::min(max_y, static_cast<i32>(draw_area_bottom_));
    min_x = std::max(min_x, static_cast<i32>(draw_area_left_));
    max_x = std::min(max_x, static_cast<i32>(draw_area_right_));

    // Edge function for barycentric coordinates
    auto edge = [](i32 ax, i32 ay, i32 bx, i32 by, i32 px, i32 py) -> i64 {
        return static_cast<i64>(bx - ax) * static_cast<i64>(py - ay) -
               static_cast<i64>(by - ay) * static_cast<i64>(px - ax);
    };

    i64 area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;  // Degenerate

    bool ccw = area < 0;
    i64 abs_area = ccw ? -area : area;

    for (i32 y = min_y; y <= max_y; y++) {
        for (i32 x = min_x; x <= max_x; x++) {
            i64 w0 = edge(x1, y1, x2, y2, x, y);
            i64 w1 = edge(x2, y2, x0, y0, x, y);
            i64 w2 = edge(x0, y0, x1, y1, x, y);

            if (ccw) { w0 = -w0; w1 = -w1; w2 = -w2; }

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                // Inside triangle
                u32 cr = r, cg = g, cb = b;
                if (shaded) {
                    // Interpolate color using barycentric coords
                    i64 iw0 = (w0 * 255) / abs_area;
                    i64 iw1 = (w1 * 255) / abs_area;
                    i64 iw2 = (w2 * 255) / abs_area;
                    // For untextured gouraud, use vertex 0 color (simplified)
                    // Full gouraud: interpolate r0,r1,r2 etc.
                    // params layout for gouraud: [c0,x0,y0, c1,x1,y1, c2,x2,y2]
                    u32 r0 = ((params[0] >> 0)  & 0xFF) >> 3;
                    u32 g0 = ((params[0] >> 8)  & 0xFF) >> 3;
                    u32 b0 = ((params[0] >> 16) & 0xFF) >> 3;
                    u32 r1 = ((params[2] >> 0)  & 0xFF) >> 3;
                    u32 g1 = ((params[2] >> 8)  & 0xFF) >> 3;
                    u32 b1 = ((params[2] >> 16) & 0xFF) >> 3;
                    u32 r2 = ((params[4] >> 0)  & 0xFF) >> 3;
                    u32 g2 = ((params[4] >> 8)  & 0xFF) >> 3;
                    u32 b2 = ((params[4] >> 16) & 0xFF) >> 3;

                    cr = static_cast<u32>((r0 * iw0 + r1 * iw1 + r2 * iw2) / 255);
                    cg = static_cast<u32>((g0 * iw0 + g1 * iw1 + g2 * iw2) / 255);
                    cb = static_cast<u32>((b0 * iw0 + b1 * iw1 + b2 * iw2) / 255);
                }

                u16 color = rgb555(cr, cg, cb);
                vram_write(static_cast<u32>(x), static_cast<u32>(y), color);
            }
        }
    }
}

void Gpu::draw_poly_quad(bool textured, bool shaded, u32* params) {
    // A quad is two triangles: (v0,v1,v2) and (v0,v2,v3).
    // For simplicity, draw two triangles.
    if (shaded) {
        // Gouraud quad: [c0,x0,y0, c1,x1,y1, c2,x2,y2, c3,x3,y3]
        u32 tri1[6] = { params[0], params[1], params[2], params[3], params[4], params[5] };
        u32 tri2[6] = { params[0], params[4], params[5], params[6], params[7], params[8] };
        draw_poly_triangle(textured, true, tri1);
        draw_poly_triangle(textured, true, tri2);
    } else {
        u32 tri1[4] = { params[0], params[1], params[2], params[3] };
        u32 tri2[4] = { params[0], params[3], params[4], params[5] };
        draw_poly_triangle(textured, false, tri1);
        draw_poly_triangle(textured, false, tri2);
    }
}

// ===========================================================================
//  Draw line
// ===========================================================================

void Gpu::draw_line(u32* params, int count) {
    // count = number of points (each point is 1 word: x,y packed or color,x,y for shaded)
    // Simplified: draw line segments between consecutive points.
    bool shaded = ((params[0] >> 24) & 0xFF) >= 0x50;

    auto get_point = [&](int idx) -> std::pair<i32, i32> {
        int word_idx = shaded ? (idx * 2 + 1) : (idx + 1);
        i32 x = static_cast<i32>(params[word_idx] & 0xFFFF);
        i32 y = static_cast<i32>((params[word_idx] >> 16) & 0xFFFF);
        if (x & 0x8000) x -= 0x10000;
        if (y & 0x8000) y -= 0x10000;
        return {x + draw_offset_x_, y + draw_offset_y_};
    };

    u32 r = ((params[0] >> 0) & 0xFF) >> 3;
    u32 g = ((params[0] >> 8) & 0xFF) >> 3;
    u32 b = ((params[0] >> 16) & 0xFF) >> 3;
    u16 color = rgb555(r, g, b);

    // Bresenham line algorithm between consecutive points.
    for (int i = 0; i < count - 1; i++) {
        auto [x0, y0] = get_point(i);
        auto [x1, y1] = get_point(i + 1);

        i32 dx = std::abs(x1 - x0);
        i32 dy = std::abs(y1 - y0);
        i32 sx = x0 < x1 ? 1 : -1;
        i32 sy = y0 < y1 ? 1 : -1;
        i32 err = dx - dy;

        while (true) {
            if (static_cast<u32>(x0) >= draw_area_left_ &&
                static_cast<u32>(x0) <= draw_area_right_ &&
                static_cast<u32>(y0) >= draw_area_top_ &&
                static_cast<u32>(y0) <= draw_area_bottom_) {
                vram_write(static_cast<u32>(x0), static_cast<u32>(y0), color);
            }
            if (x0 == x1 && y0 == y1) break;
            i32 e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    }
}

// ===========================================================================
//  Draw rectangle
// ===========================================================================

void Gpu::draw_rect(u32* params) {
    u32 r = ((params[0] >> 0) & 0xFF) >> 3;
    u32 g = ((params[0] >> 8) & 0xFF) >> 3;
    u32 b = ((params[0] >> 16) & 0xFF) >> 3;
    u16 color = rgb555(r, g, b);

    i32 x = static_cast<i32>(params[1] & 0xFFFF);
    i32 y = static_cast<i32>((params[1] >> 16) & 0xFFFF);
    if (x & 0x8000) x -= 0x10000;
    if (y & 0x8000) y -= 0x10000;
    x += draw_offset_x_; y += draw_offset_y_;

    u32 w = (params[2] & 0xFFFF) + 1;
    u32 h = ((params[2] >> 16) & 0xFFFF) + 1;

    // Clip to drawing area
    u32 sx = static_cast<u32>(std::max(x, static_cast<i32>(draw_area_left_)));
    u32 sy = static_cast<u32>(std::max(y, static_cast<i32>(draw_area_top_)));
    u32 ex = static_cast<u32>(std::min(static_cast<i32>(x + w - 1), static_cast<i32>(draw_area_right_)));
    u32 ey = static_cast<u32>(std::min(static_cast<i32>(y + h - 1), static_cast<i32>(draw_area_bottom_)));

    for (u32 row = sy; row <= ey; row++) {
        for (u32 col = sx; col <= ex; col++) {
            vram_write(col, row, color);
        }
    }
}

void Gpu::draw_rect_var_size(u32* params) {
    // Variable-size rectangle: same as draw_rect but w/h are in the 3rd word.
    draw_rect(params);
}

// ===========================================================================
//  Command dispatch
// ===========================================================================

void Gpu::process_command() {
    if (cmd_buffer_.empty()) return;
    u32 opcode = cmd_buffer_[0];
    u32 cmd = (opcode >> 24) & 0xFF;
    u32* params = cmd_buffer_.data();

    switch (cmd) {
        case 0x00: cmd_nop(); break;
        case 0x01: cmd_clear_cache(); break;
        case 0x02: // Fill rectangle
            {
                u16 color = static_cast<u16>(opcode & 0xFFFF);
                u32 x = params[1] & 0xFFFF;
                u32 y = (params[1] >> 16) & 0xFFFF;
                u32 w = params[2] & 0xFFFF;
                u32 h = (params[2] >> 16) & 0xFFFF;
                fill_rect(x, y, w, h, color);
            }
            break;
        // Triangles
        case 0x20: case 0x21: case 0x22: case 0x23:
            draw_poly_triangle(false, false, params);
            break;
        case 0x24: case 0x25: case 0x26: case 0x27:
            draw_poly_triangle(true, false, params);
            break;
        case 0x28: case 0x29: case 0x2A: case 0x2B:
            draw_poly_quad(false, false, params);
            break;
        case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            draw_poly_quad(true, false, params);
            break;
        // Gouraud triangles
        case 0x30: case 0x31: case 0x32: case 0x33:
            draw_poly_triangle(false, true, params);
            break;
        case 0x34: case 0x35: case 0x36: case 0x37:
            draw_poly_triangle(true, true, params);
            break;
        case 0x38: case 0x39: case 0x3A: case 0x3B:
            draw_poly_quad(false, true, params);
            break;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            draw_poly_quad(true, true, params);
            break;
        // Lines
        case 0x40: case 0x41: case 0x42: case 0x43:
            draw_line(params, 2); break;
        case 0x48: case 0x49: case 0x4A: case 0x4B:
            draw_line(params, 2); break;
        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            draw_line(params, 3); break;
        case 0x50: case 0x51: case 0x52: case 0x53:
            draw_line(params, 4); break;
        case 0x54: case 0x55: case 0x56: case 0x57:
            draw_line(params, 5); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B:
            draw_line(params, 6); break;
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            draw_line(params, 7); break;
        // Rectangles
        case 0x60: case 0x61: case 0x62: case 0x63:
            draw_rect_var_size(params); break;
        case 0x64: case 0x65: case 0x66: case 0x67:
            // 1x1 textured sprite
            {
                i32 x = params[1] & 0xFFFF; if (x & 0x8000) x -= 0x10000;
                i32 y = (params[1] >> 16) & 0xFFFF; if (y & 0x8000) y -= 0x10000;
                x += draw_offset_x_; y += draw_offset_y_;
                u8 u = params[2] & 0xFF;
                u8 v = (params[2] >> 8) & 0xFF;
                u16 texel = sample_texture(u, v);
                vram_write(static_cast<u32>(x), static_cast<u32>(y), texel);
            }
            break;
        case 0x68: case 0x69: case 0x6A: case 0x6B:
            // 8x8 textured sprite
            {
                i32 x = params[1] & 0xFFFF; if (x & 0x8000) x -= 0x10000;
                i32 y = (params[1] >> 16) & 0xFFFF; if (y & 0x8000) y -= 0x10000;
                x += draw_offset_x_; y += draw_offset_y_;
                u8 u = params[2] & 0xFF;
                u8 v = (params[2] >> 8) & 0xFF;
                u16 color = static_cast<u16>(params[0] & 0xFFFF);
                for (int dy = 0; dy < 8; dy++) {
                    for (int dx = 0; dx < 8; dx++) {
                        u16 texel = sample_texture(static_cast<u8>(u + dx), static_cast<u8>(v + dy));
                        if (texel != 0) {  // Skip transparent
                            vram_write(static_cast<u32>(x + dx), static_cast<u32>(y + dy), texel);
                        } else {
                            vram_write(static_cast<u32>(x + dx), static_cast<u32>(y + dy), color);
                        }
                    }
                }
            }
            break;
        case 0x6C: case 0x6D: case 0x6E: case 0x6F:
            // 16x16 textured sprite
            {
                i32 x = params[1] & 0xFFFF; if (x & 0x8000) x -= 0x10000;
                i32 y = (params[1] >> 16) & 0xFFFF; if (y & 0x8000) y -= 0x10000;
                x += draw_offset_x_; y += draw_offset_y_;
                u8 u = params[2] & 0xFF;
                u8 v = (params[2] >> 8) & 0xFF;
                for (int dy = 0; dy < 16; dy++) {
                    for (int dx = 0; dx < 16; dx++) {
                        u16 texel = sample_texture(static_cast<u8>(u + dx), static_cast<u8>(v + dy));
                        vram_write(static_cast<u32>(x + dx), static_cast<u32>(y + dy), texel);
                    }
                }
            }
            break;
        case 0x70: case 0x71: case 0x72: case 0x73:
            // Variable-size textured sprite
            draw_rect_var_size(params);
            break;

        // ---- VRAM operations ----
        case 0x80: {
            // VRAM write — header only; data follows via subsequent GP0 writes.
            // params[1] = x,y; params[2] = w,h
            vram_write_start_x_ = params[1] & 0xFFFF;
            vram_write_start_y_ = (params[1] >> 16) & 0xFFFF;
            u32 w = params[2] & 0xFFFF;
            u32 h = (params[2] >> 16) & 0xFFFF;
            vram_write_end_x_ = vram_write_start_x_ + w;
            vram_write_x_ = vram_write_start_x_;
            vram_write_y_ = vram_write_start_y_;
            vram_write_remaining_ = w * h;
            // Don't clear cmd_buffer_ — we keep receiving data.
            cmd_buffer_.clear();
            cmd_buffer_.push_back(opcode); // Keep the command word.
            cmd_words_ = 1;
            cmd_len_ = 999999;  // Prevent process_command from firing.
            break;
        }
        case 0xA0: {
            // VRAM read
            vram_read_start_x_ = params[1] & 0xFFFF;
            vram_read_start_y_ = (params[1] >> 16) & 0xFFFF;
            u32 w = params[2] & 0xFFFF;
            u32 h = (params[2] >> 16) & 0xFFFF;
            vram_read_end_x_ = vram_read_start_x_ + w;
            vram_read_x_ = vram_read_start_x_;
            vram_read_y_ = vram_read_start_y_;
            vram_read_remaining_ = w * h;
            break;
        }
        case 0xC0: {
            // VRAM-to-VRAM copy
            u32 sx = params[1] & 0xFFFF;
            u32 sy = (params[1] >> 16) & 0xFFFF;
            u32 dx = params[2] & 0xFFFF;
            u32 dy = (params[2] >> 16) & 0xFFFF;
            u32 w = params[3] & 0xFFFF;
            u32 h = (params[3] >> 16) & 0xFFFF;
            std::vector<u16> tmp(w * h);
            vram_read_rect(sx, sy, w, h, tmp.data());
            vram_write_rect(dx, dy, w, h, tmp.data());
            break;
        }

        // ---- GPU internal state commands ----
        case 0xE1: {
            // Texture page setting
            tex_page_x_ = ((params[1] & 0x0F) << 6);   // bits 3:0 * 64
            tex_page_y_ = ((params[1] & 0x10) << 2);   // bit 4 * 512
            tex_trans_mode_ = (params[1] >> 5) & 3;
            break;
        }
        case 0xE2: {
            // Texture window setting
            tex_win_x_ = params[1] & 0x1F;
            tex_win_y_ = (params[1] >> 5) & 0x1F;
            tex_win_w_ = ((params[1] >> 10) & 0x1F) << 3;
            tex_win_h_ = ((params[1] >> 15) & 0x1F) << 3;
            if (tex_win_w_ == 0) tex_win_w_ = 256;
            if (tex_win_h_ == 0) tex_win_h_ = 256;
            break;
        }
        case 0xE3: {
            // Drawing area top-left
            draw_area_left_  = params[1] & 0x3FF;
            draw_area_top_   = (params[1] >> 16) & 0x3FF;
            break;
        }
        case 0xE4: {
            // Drawing area bottom-right
            draw_area_right_ = params[1] & 0x3FF;
            draw_area_bottom_ = (params[1] >> 16) & 0x3FF;
            break;
        }
        case 0xE5: {
            // Drawing offset
            i32 ox = params[1] & 0xFFFF;
            i32 oy = (params[1] >> 16) & 0xFFFF;
            if (ox & 0x8000) ox -= 0x10000;
            if (oy & 0x8000) oy -= 0x10000;
            draw_offset_x_ = ox;
            draw_offset_y_ = oy;
            break;
        }
        case 0xE6: {
            // Mask bit setting
            dither_ = !(params[1] & 1);
            break;
        }
        default:
            LOG_DEBUG("Unhandled GP0 command {:02X}", cmd);
            break;
    }
}

void Gpu::cmd_nop() {}

void Gpu::cmd_clear_cache() {
    // Clear texture cache (no-op for software renderer).
}

// ===========================================================================
//  DMA interface
// ===========================================================================

void Gpu::dma_write(const u8* data, u32 words) {
    // Feed words to GP0.
    for (u32 i = 0; i < words; i++) {
        u32 val;
        std::memcpy(&val, data + i * 4, 4);
        write_gp0(val);
    }
}

void Gpu::dma_linked_list(u32 addr, BusInterface* bus) {
    // Follow a linked list in RAM: each entry is (next_addr | 0x800000, word_count, data...)
    u32 current = addr & 0x1FFFFF;
    while ((current & 0x800000) == 0) {
        u32 header = bus->read32(current);
        u32 next = header & 0x00FFFFFF;
        u32 count = (header >> 24) & 0xFF;
        current += 4;
        for (u32 i = 0; i < count; i++) {
            u32 val = bus->read32(current);
            write_gp0(val);
            current += 4;
        }
        current = next;
    }
}

// ===========================================================================
//  VRAM transfer state (declared here for the write_gp0 VRAM path)
// ===========================================================================
// These are instance variables used during VRAM write/read transfers.
// We declare them as part of the class (they're used in write_gp0/read_gp0).

} // namespace yaps1