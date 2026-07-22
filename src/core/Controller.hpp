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

class Controller {
public:
    void set(Button button, bool pressed) {
        const u8 bit = static_cast<u8>(button);
        if (pressed) {
            state_ |= static_cast<u8>(1u << bit);
        } else {
            state_ &= static_cast<u8>(~(1u << bit));
        }
    }

    void strobe(u8 value) {
        strobe_ = (value & 1u) != 0;
        if (strobe_) {
            shift_ = state_;
        }
    }

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
