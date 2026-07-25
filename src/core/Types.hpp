#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nes {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i16 = std::int16_t;

constexpr int kScreenWidth = 256;
constexpr int kScreenHeight = 240;
constexpr int kCpuFrequencyNtsc = 1'789'773;
constexpr double kFrameRateNtsc = 60.0988138974405;

struct Rgb {
    u8 r = 0;
    u8 g = 0;
    u8 b = 0;
    u8 a = 255;
};

using Framebuffer = std::array<Rgb, kScreenWidth * kScreenHeight>;

} // namespace nes
