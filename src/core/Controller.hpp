#pragma once

#include "Types.hpp"

namespace nes {

enum class Button : u8 {
    A = 0,
    B,
    Select,
    Start,
    Up,
    Down,
    Left,
    Right,
};

// NES controller shift register. The frontend updates state_, then CPU reads serial bits.
class Controller {
public:
    // Set or clear one physical button in the latched controller state.
    void set(Button button, bool pressed) {
        const u8 bit = static_cast<u8>(button);
        if (pressed) {
            state_ |= static_cast<u8>(1u << bit);
        } else {
            state_ &= static_cast<u8>(~(1u << bit));
        }
    }

    // Writes to $4016 control whether reads return live A-button state or shift latched bits.
    void strobe(u8 value) {
        strobe_ = (value & 1u) != 0;
        if (strobe_) {
            shift_ = state_;
        }
    }

    // Return one controller bit in the NES register format, then advance the shift register.
    u8 read() {
        if (strobe_) {
            return static_cast<u8>(0x40 | (state_ & 1u));
        }
        const u8 out = static_cast<u8>(0x40 | (shift_ & 1u));
        shift_ = static_cast<u8>(0x80u | (shift_ >> 1u));
        return out;
    }

private:
    u8 state_ = 0;
    u8 shift_ = 0;
    bool strobe_ = false;
};

} // namespace nes
