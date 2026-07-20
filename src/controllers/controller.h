#pragma once

/// @file controller.h
/// @brief PS1 controller (gamepad) and memory card interface via SDL3.
///        Handles both controller and memory card I/O through the
///        JOY_CTRL / JOY_STAT / JOY_TX/RX registers.

#include "common/types.h"
#include <array>
#include <string>
#include <unordered_map>

// Forward-declare SDL types to avoid pulling SDL.h in the header.
struct SDL_Gamepad;
typedef struct SDL_Gamepad SDL_Gamepad;

namespace yaps1 {

class MemoryCard;

/// Manages gamepad input via SDL3.  Handles the JoyPad I/O protocol
/// that the PS1 BIOS uses to communicate with controllers and memory cards.
class Pad {
public:
    Pad();
    ~Pad();

    void reset();
    void set_memcard(MemoryCard* mc) { memcard_ = mc; }

    /// Initialize SDL3 joystick/gamecontroller subsystem.
    void init_sdl();

    /// Poll SDL for new input events. Call once per frame.
    void update();

    /// I/O register interface (offsets from 0x1F801040).
    void write8(u32 offset, u8 val);
    void write16(u32 offset, u16 val);
    void write32(u32 offset, u32 val);
    [[nodiscard]] u8  read8(u32 offset);
    [[nodiscard]] u16 read16(u32 offset);
    [[nodiscard]] u32 read32(u32 offset);

    // ---- Keyboard mapping ----
    void set_key_mapping(const std::unordered_map<std::string, int>& keys);

    // ---- State query ----
    [[nodiscard]] bool button_pressed(int btn) const { return buttons_ & (1u << btn); }
    [[nodiscard]] bool exit_requested() const { return exit_requested_; }

private:
    bool exit_requested_ = false;
    // PS1 digital controller buttons (bit indices).
    enum Button {
        SELECT = 0,
        L3     = 1,
        R3     = 2,
        START  = 3,
        UP     = 4,
        RIGHT  = 5,
        DOWN   = 6,
        LEFT   = 7,
        L2     = 8,
        R2     = 9,
        L1     = 10,
        R1     = 11,
        TRIANGLE = 12,
        CIRCLE = 13,
        CROSS  = 14,
        SQUARE = 15,
    };

    // ---- Button state ----
    u16 buttons_ = 0xFFFF;  // Active low (1 = not pressed)

    // ---- Keyboard to PS1 button mapping ----
    int key_cross_    = 0;
    int key_circle_   = 0;
    int key_square_   = 0;
    int key_triangle_ = 0;
    int key_l1_ = 0, key_r1_ = 0;
    int key_l2_ = 0, key_r2_ = 0;
    int key_select_ = 0, key_start_ = 0;
    int key_up_ = 0, key_down_ = 0, key_left_ = 0, key_right_ = 0;
    int key_l3_ = 0, key_r3_ = 0;

    // ---- Joy I/O protocol state ----
    enum class JoyState { Idle, Transmit, Receive, Acknowledge };
    JoyState joy_state_ = JoyState::Idle;
    u8 joy_ctrl_ = 0;
    u16 joy_stat_ = 0x5A02;  // TX ready = 1, RX empty = 1
    u8 joy_baud_ = 0;
    std::array<u8, 8> joy_tx_fifo_{};
    std::array<u8, 8> joy_rx_fifo_{};
    int joy_tx_count_ = 0;
    int joy_rx_count_ = 0;
    int joy_rx_read_ = 0;

    // ---- Controller data response ----
    u8  ctrl_response_[8]{};
    int ctrl_response_idx_ = 0;
    int ctrl_response_len_ = 0;
    bool ctrl_active_ = false;
    int  ctrl_slot_ = 0;    // 0 = controller, 1 = memory card

    // ---- SDL controller ----
    SDL_Gamepad* sdl_controller_ = nullptr;
    int  sdl_instance_id_ = -1;

    // ---- Memory card pointer ----
    MemoryCard* memcard_ = nullptr;

    // ---- Internal ----
    void handle_tx(u8 val);
    void prepare_controller_response(u8 cmd);
    void prepare_memcard_response(u8 cmd);

    /// Scan for and open a connected SDL game controller.
    void scan_controllers();
};

} // namespace yaps1