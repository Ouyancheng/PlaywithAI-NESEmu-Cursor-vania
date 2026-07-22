#pragma once

#include "APU.hpp"
#include "Cartridge.hpp"
#include "Controller.hpp"
#include "PPU.hpp"
#include "Types.hpp"

#include <array>

namespace nes {

class Bus {
public:
    void insertCartridge(Cartridge* cartridge);
    void reset();
    u8 cpuRead(u16 address);
    void cpuWrite(u16 address, u8 data);
    void clock();
    void clockCpuDevices();
    bool cpuStalled() const { return dmaCycles_ > 0; }
    void tickCpuDmaStall();

    PPU& ppu() { return ppu_; }
    const PPU& ppu() const { return ppu_; }
    APU& apu() { return apu_; }
    const APU& apu() const { return apu_; }
    Controller& controller(int index) { return controllers_[index]; }
    Cartridge* cartridge() { return cartridge_; }

private:
    std::array<u8, 2048> ram_{};
    std::array<Controller, 2> controllers_{};
    Cartridge* cartridge_ = nullptr;
    PPU ppu_;
    APU apu_;
    int dmaCycles_ = 0;
};

} // namespace nes
