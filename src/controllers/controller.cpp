/// @file controller.cpp
/// @brief PS1 controller (gamepad) and memory card pad interface.

#include "controllers/controller.h"
#include "common/logger.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

namespace yaps1 {

// ===========================================================================
//  Constructor / Destructor
// ===========================================================================

Pad::Pad() { reset(); }

Pad::~Pad() {
    if (sdl_controller_) {
        SDL_CloseGamepad(sdl_controller_);
        sdl_controller_ = nullptr;
    }
}

void Pad::reset() {
    buttons_ = 0xFFFF;
    exit_requested_ = false;
    joy_state_ = JoyState::Idle;
    joy_ctrl_ = 0;
    joy_stat_ = 0x5A02;
    joy_baud_ = 0;
    joy_tx_fifo_.fill(0);
    joy_rx_fifo_.fill(0);
    joy_tx_count_ = 0;
    joy_rx_count_ = 0;
    joy_rx_read_ = 0;
    ctrl_response_idx_ = 0;
    ctrl_response_len_ = 0;
    ctrl_active_ = false;
    ctrl_slot_ = 0;

    // Default keyboard mapping.
    key_cross_    = SDL_SCANCODE_SPACE;
    key_circle_   = SDL_SCANCODE_L;
    key_square_   = SDL_SCANCODE_K;
    key_triangle_ = SDL_SCANCODE_I;
    key_l1_ = SDL_SCANCODE_Q;  key_r1_ = SDL_SCANCODE_E;
    key_l2_ = SDL_SCANCODE_1;  key_r2_ = SDL_SCANCODE_3;
    key_select_ = SDL_SCANCODE_RSHIFT;
    key_start_  = SDL_SCANCODE_RETURN;
    key_up_    = SDL_SCANCODE_W;
    key_down_  = SDL_SCANCODE_S;
    key_left_  = SDL_SCANCODE_A;
    key_right_ = SDL_SCANCODE_D;
    key_l3_ = 0; key_r3_ = 0;
}

// ===========================================================================
//  SDL init
// ===========================================================================

void Pad::init_sdl() {
    // SDL is initialized by the frontend. We just scan for controllers.
    scan_controllers();
}

void Pad::scan_controllers() {
    if (sdl_controller_) {
        SDL_CloseGamepad(sdl_controller_);
        sdl_controller_ = nullptr;
        sdl_instance_id_ = -1;
    }

    // Try to open the first available game controller.
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        sdl_controller_ = SDL_OpenGamepad(ids[0]);
        if (sdl_controller_) {
            sdl_instance_id_ = static_cast<int>(ids[0]);
            const char* name = SDL_GetGamepadName(sdl_controller_);
            LOG_INFO("Controller connected: {}", name ? name : "Unknown");
        }
    }
    if (ids) SDL_free(ids);
}

// ===========================================================================
//  Input polling
// ===========================================================================

void Pad::update() {
    // Poll SDL for events (already done in the main loop, but we check state).

    // Read keyboard state.
    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Build button state (active low).
    u16 state = 0xFFFF;
    if (keys[key_cross_])    state &= ~(1u << CROSS);
    if (keys[key_circle_])   state &= ~(1u << CIRCLE);
    if (keys[key_square_])   state &= ~(1u << SQUARE);
    if (keys[key_triangle_]) state &= ~(1u << TRIANGLE);
    if (keys[key_l1_]) state &= ~(1u << L1);
    if (keys[key_r1_]) state &= ~(1u << R1);
    if (keys[key_l2_]) state &= ~(1u << L2);
    if (keys[key_r2_]) state &= ~(1u << R2);
    if (keys[key_select_]) state &= ~(1u << SELECT);
    if (keys[key_start_])  state &= ~(1u << START);
    if (keys[key_up_])    state &= ~(1u << UP);
    if (keys[key_down_])  state &= ~(1u << DOWN);
    if (keys[key_left_])  state &= ~(1u << LEFT);
    if (keys[key_right_]) state &= ~(1u << RIGHT);
    if (keys[key_l3_])    state &= ~(1u << L3);
    if (keys[key_r3_])    state &= ~(1u << R3);

    // Read SDL game controller buttons if connected.
    if (sdl_controller_) {
        auto btn = [&](SDL_GamepadButton sdl_btn, Button ps1_btn) {
            if (SDL_GetGamepadButton(sdl_controller_, sdl_btn)) {
                state &= ~(1u << ps1_btn);
            }
        };
        btn(SDL_GAMEPAD_BUTTON_SOUTH,     CROSS);
        btn(SDL_GAMEPAD_BUTTON_EAST,      CIRCLE);
        btn(SDL_GAMEPAD_BUTTON_WEST,      SQUARE);
        btn(SDL_GAMEPAD_BUTTON_NORTH,     TRIANGLE);
        btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  L1);
        btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, R1);
        btn(SDL_GAMEPAD_BUTTON_BACK,      SELECT);
        btn(SDL_GAMEPAD_BUTTON_START,     START);
        btn(SDL_GAMEPAD_BUTTON_LEFT_STICK,  L3);
        btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, R3);

        // D-pad from axes.
        float lx = SDL_GetGamepadAxis(sdl_controller_, SDL_GAMEPAD_AXIS_LEFTX);
        float ly = SDL_GetGamepadAxis(sdl_controller_, SDL_GAMEPAD_AXIS_LEFTY);
        if (ly < -0.5f) state &= ~(1u << UP);
        if (ly >  0.5f) state &= ~(1u << DOWN);
        if (lx < -0.5f) state &= ~(1u << LEFT);
        if (lx >  0.5f) state &= ~(1u << RIGHT);

        // L2/R2 from triggers.
        float lt = SDL_GetGamepadAxis(sdl_controller_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        float rt = SDL_GetGamepadAxis(sdl_controller_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (lt > 0.5f) state &= ~(1u << L2);
        if (rt > 0.5f) state &= ~(1u << R2);
    }

    buttons_ = state;

    // Handle controller hot-plug and window exit events.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            exit_requested_ = true;
        } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (sdl_controller_ == nullptr) {
                scan_controllers();
            }
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (sdl_instance_id_ == static_cast<int>(event.gdevice.which)) {
                SDL_CloseGamepad(sdl_controller_);
                sdl_controller_ = nullptr;
                sdl_instance_id_ = -1;
                LOG_INFO("Controller disconnected");
                // Try to find another one.
                scan_controllers();
            }
        }
    }
}

// ===========================================================================
//  Key mapping
// ===========================================================================

void Pad::set_key_mapping(const std::unordered_map<std::string, int>& keys) {
    auto get = [&](const std::string& name) -> int {
        auto it = keys.find(name);
        return (it != keys.end()) ? it->second : 0;
    };
    key_cross_    = get("cross");
    key_circle_   = get("circle");
    key_square_   = get("square");
    key_triangle_ = get("triangle");
    key_l1_ = get("l1");  key_r1_ = get("r1");
    key_l2_ = get("l2");  key_r2_ = get("r2");
    key_select_ = get("select");
    key_start_  = get("start");
    key_up_    = get("up");    key_down_  = get("down");
    key_left_  = get("left");  key_right_ = get("right");
    key_l3_ = get("l3"); key_r3_ = get("r3");
}

// ===========================================================================
//  Joy I/O protocol
// ===========================================================================

void Pad::write8(u32 offset, u8 val) {
    switch (offset) {
        case 0x00: // JOY_TX_DATA
            if (joy_tx_count_ < 8) {
                joy_tx_fifo_[joy_tx_count_++] = val;
            }
            break;
        case 0x04: // JOY_STAT (some bits writable)
            joy_stat_ = (joy_stat_ & 0x07) | (val & 0xF8);
            break;
        case 0x08: // JOY_MODE
            joy_baud_ = val;
            break;
        case 0x0A: // JOY_CTRL
            joy_ctrl_ = val;
            if (val & 0x10) {  // ACK
                joy_rx_read_ = 0;
                joy_rx_count_ = 0;
                ctrl_active_ = false;
            }
            if (val & 0x20) {  // Reset
                joy_rx_read_ = 0;
                joy_rx_count_ = 0;
                ctrl_active_ = false;
                ctrl_response_idx_ = 0;
                joy_ctrl_ &= ~0x20;
            }
            if (val & 0x01) {  // TX enable
                // Select device: bit 2 = 1 = memory card, 0 = controller
                ctrl_slot_ = (val & 0x04) ? 1 : 0;

                if (joy_tx_count_ > 0) {
                    u8 tx_val = joy_tx_fifo_[0];
                    joy_tx_count_ = 0;

                    if (ctrl_slot_ == 0) {
                        prepare_controller_response(tx_val);
                    } else {
                        prepare_memcard_response(tx_val);
                    }

                    ctrl_active_ = true;
                    joy_stat_ |= 0x02;  // RX not empty
                    joy_stat_ &= ~0x01;  // TX not full
                }
            }
            break;
    }
}

void Pad::write16(u32 offset, u16 val) {
    write8(offset,     static_cast<u8>(val & 0xFF));
    write8(offset + 1, static_cast<u8>((val >> 8) & 0xFF));
}

void Pad::write32(u32 offset, u32 val) {
    write16(offset,      static_cast<u16>(val & 0xFFFF));
    write16(offset + 2,  static_cast<u16>((val >> 16) & 0xFFFF));
}

u8 Pad::read8(u32 offset) {
    switch (offset) {
        case 0x00: // JOY_RX_DATA
            if (joy_rx_read_ < joy_rx_count_) {
                u8 val = joy_rx_fifo_[joy_rx_read_++];
                if (joy_rx_read_ >= joy_rx_count_) {
                    joy_stat_ &= ~0x02;  // RX empty
                }
                return val;
            }
            return 0xFF;
        case 0x04: return static_cast<u8>(joy_stat_);
        case 0x08: return joy_baud_;
        case 0x0A: return joy_ctrl_;
    }
    return 0xFF;
}

u16 Pad::read16(u32 offset) {
    return (static_cast<u16>(read8(offset + 1)) << 8) | read8(offset);
}

u32 Pad::read32(u32 offset) {
    return (static_cast<u32>(read16(offset + 2)) << 16) | read16(offset);
}

// ===========================================================================
//  Controller response
// ===========================================================================

void Pad::prepare_controller_response(u8 cmd) {
    joy_rx_count_ = 0;
    joy_rx_read_ = 0;

    switch (cmd & 0x5F) {
        case 0x40: { // Read controller info
            // ID byte, then two bytes of button state.
            joy_rx_fifo_[0] = 0x41;       // Controller ID (no analog)
            joy_rx_fifo_[1] = 0x5A;       // No motor
            joy_rx_fifo_[2] = static_cast<u8>(buttons_ & 0xFF);
            joy_rx_fifo_[3] = static_cast<u8>((buttons_ >> 8) & 0xFF);
            joy_rx_count_ = 4;
            break;
        }
        case 0x41: { // Read data (buttons)
            joy_rx_fifo_[0] = 0x41;
            joy_rx_fifo_[1] = 0x5A;
            joy_rx_fifo_[2] = static_cast<u8>(buttons_ & 0xFF);
            joy_rx_fifo_[3] = static_cast<u8>((buttons_ >> 8) & 0xFF);
            joy_rx_count_ = 4;
            break;
        }
        case 0x42: // Config mode
            joy_rx_fifo_[0] = 0x00;
            joy_rx_fifo_[1] = 0x00;
            joy_rx_count_ = 2;
            break;
        case 0x43: { // Set mode / analog query
            joy_rx_fifo_[0] = 0x00;
            joy_rx_fifo_[1] = 0x00;
            joy_rx_fifo_[2] = 0x00;
            joy_rx_fifo_[3] = 0x00;
            joy_rx_count_ = 4;
            break;
        }
        default:
            joy_rx_fifo_[0] = 0xFF;
            joy_rx_fifo_[1] = 0xFF;
            joy_rx_count_ = 2;
            break;
    }
}

// ===========================================================================
//  Memory card response (stub — delegates to MemoryCard subsystem)
// ===========================================================================

void Pad::prepare_memcard_response(u8 cmd) {
    // The memory card uses a separate SPI-like protocol.
    // We respond with basic ACKs here.  The MemoryCard class handles
    // the actual data operations.
    joy_rx_count_ = 0;
    joy_rx_read_ = 0;

    if (!memcard_) {
        joy_rx_fifo_[0] = 0xFF;  // No card
        joy_rx_count_ = 1;
        return;
    }

    // Basic memory card ID response.
    // cmd 0x52 = read, 0x57 = write, 0x53 = get ID.
    switch (cmd) {
        case 0x53: // Memory card ID
            joy_rx_fifo_[0] = 0x5A;  // ACK
            joy_rx_fifo_[1] = 0x5A;  // Second ACK
            joy_rx_fifo_[2] = 0x00;  // Sector size MSB
            joy_rx_fifo_[3] = 0x00;  // Sector size
            joy_rx_fifo_[4] = 0x80;  // 128 bytes per sector
            joy_rx_fifo_[5] = 0x00;
            joy_rx_fifo_[6] = 0x00;
            joy_rx_fifo_[7] = 0x00;
            joy_rx_count_ = 8;
            break;
        default:
            joy_rx_fifo_[0] = 0x5A;  // Generic ACK
            joy_rx_count_ = 1;
            break;
    }
}

} // namespace yaps1