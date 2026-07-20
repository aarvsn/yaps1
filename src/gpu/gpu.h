#pragma once

/// @file gpu.h
/// @brief PS1 GPU — software renderer with full command implementation.

#include "common/types.h"
#include <array>
#include <vector>

namespace yaps1 {

class BusInterface;

/// The PlayStation 1 GPU (custom Sony chip).
/// Implements the complete GP0/GP1 command set with a software renderer
/// that outputs to an internal VRAM framebuffer.
class Gpu {
public:
    Gpu();
    ~Gpu() = default;

    void reset();
    void set_bus(BusInterface* bus) { bus_ = bus; }

    // ---- GP0 / GP1 register interface (called by Bus) ----
    void write_gp0(u32 val);
    void write_gp1(u32 val);
    [[nodiscard]] u32 read_gp0();
    [[nodiscard]] u32 read_gp1();

    // ---- DMA interface ----
    /// DMA block transfer to GPU (from RAM to VRAM via GP0).
    void dma_write(const u8* data, u32 words);
    /// DMA linked-list mode.
    void dma_linked_list(u32 addr, BusInterface* bus);

    // ---- Framebuffer access ----
    [[nodiscard]] const u16* vram() const { return vram_.data(); }
    [[nodiscard]] u16*       vram()       { return vram_.data(); }
    [[nodiscard]] u32        vram_width()  const { return VRAM_WIDTH; }
    [[nodiscard]] u32        vram_height() const { return VRAM_HEIGHT; }

    // ---- Timing ----
    /// Number of dot clocks consumed since last frame.
    void tick(u32 dot_clocks);
    /// Returns true when a new frame is ready (VBlank start).
    [[nodiscard]] bool frame_ready() const { return frame_ready_; }
    void clear_frame_ready() { frame_ready_ = false; }
    [[nodiscard]] bool in_vblank() const { return in_vblank_; }

    // ---- Interrupt ----
    [[nodiscard]] bool irq_pending() const { return irq_pending_; }
    void clear_irq() { irq_pending_ = false; }

    // ---- State (for save-states) ----
    [[nodiscard]] u32  stat() const { return stat_; }
    void set_stat(u32 s) { stat_ = s; }

private:
    // ---- Constants ----
    static constexpr u32 VRAM_WIDTH  = 1024;
    static constexpr u32 VRAM_HEIGHT = 512;
    static constexpr u32 VRAM_SIZE   = VRAM_WIDTH * VRAM_HEIGHT;

    // ---- VRAM ----
    std::array<u16, VRAM_SIZE> vram_{};

    // ---- GP0 command state ----
    std::vector<u32> cmd_buffer_;
    int cmd_len_ = 0;       // expected word count for current command
    int cmd_words_ = 0;     // words received so far

    // ---- GPU status register (GP1(0x10)) ----
    u32 stat_ = 0;

    // ---- Internal drawing state ----
    // Texture page base (from GP0(E1h))
    u32 tex_page_x_ = 0;
    u32 tex_page_y_ = 0;
    // Transparency mode
    u32 tex_trans_mode_ = 0;
    // Texture window
    u32 tex_win_x_ = 0, tex_win_y_ = 0;
    u32 tex_win_w_ = 256, tex_win_h_ = 256;
    // Drawing area
    u32 draw_area_left_ = 0, draw_area_top_ = 0;
    u32 draw_area_right_ = 255, draw_area_bottom_ = 239;
    // Drawing offset
    i32 draw_offset_x_ = 0, draw_offset_y_ = 0;
    // Dithering
    bool dither_ = true;

    // ---- Timing state ----
    u32 dot_clocks_ = 0;
    u32 hclock_ = 0;
    u32 vline_ = 0;
    bool in_vblank_ = false;
    bool frame_ready_ = false;
    bool irq_pending_ = false;

    // ---- VRAM transfer state ----
    u32 vram_write_start_x_ = 0, vram_write_start_y_ = 0;
    u32 vram_write_end_x_ = 0, vram_write_y_ = 0;
    u32 vram_write_x_ = 0;
    u32 vram_write_remaining_ = 0;
    u32 vram_read_start_x_ = 0, vram_read_start_y_ = 0;
    u32 vram_read_end_x_ = 0, vram_read_y_ = 0;
    u32 vram_read_x_ = 0;
    u32 vram_read_remaining_ = 0;

    // ---- Command dispatch ----
    void process_command();
    void cmd_nop();
    void cmd_clear_cache();

    // ---- GP1 commands ----
    void gp1_reset_cmd();
    void gp1_reset_int();
    void gp1_display_enable(u32 val);
    void gp1_dma_dir(u32 val);
    void gp1_display_mode(u32 val);
    void gp1_display_area(u32 val);
    void gp1_horiz_display(u32 val);
    void gp1_vert_display(u32 val);

    // ---- Drawing primitives ----
    void fill_rect(u32 x, u32 y, u32 w, u32 h, u16 color);
    void draw_poly_triangle(bool textured, bool shaded, u32* params);
    void draw_poly_quad(bool textured, bool shaded, u32* params);
    void draw_line(u32* params, int count);
    void draw_rect(u32* params);
    void draw_rect_var_size(u32* params);

    // ---- VRAM helpers ----
    [[nodiscard]] u16 vram_read(u32 x, u32 y) const;
    void vram_write(u32 x, u32 y, u16 color);
    void vram_read_rect(u32 x, u32 y, u32 w, u32 h, u16* dst);
    void vram_write_rect(u32 x, u32 y, u32 w, u32 h, const u16* src);

    // ---- Color helpers ----
    [[nodiscard]] u16 rgb555(u32 r, u32 g, u32 b) const {
        return static_cast<u16>(((r & 0x1F) << 10) | ((g & 0x1F) << 5) | (b & 0x1F));
    }
    [[nodiscard]] u32 r5(u16 c) const { return (c >> 10) & 0x1F; }
    [[nodiscard]] u32 g5(u16 c) const { return (c >> 5)  & 0x1F; }
    [[nodiscard]] u32 b5(u16 c) const { return c & 0x1F; }

    // ---- Clipping ----
    [[nodiscard]] bool clip_to_area(i32& x, i32& y) const;

    // ---- Dithering ----
    void dither(i32 x, i32 y, u32& r, u32& g, u32& b);

    // ---- Texture sampling ----
    u16 sample_texture(u8 u, u8 v);

    BusInterface* bus_ = nullptr;
};

} // namespace yaps1