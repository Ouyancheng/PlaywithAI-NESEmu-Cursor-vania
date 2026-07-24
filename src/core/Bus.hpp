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
    // Attach the cartridge to CPU/PPU address spaces and configure DMC sample reads.
    void insertCartridge(Cartridge* cartridge);
    // Reset RAM, PPU/APU state, DMA stall state, and the active cartridge.
    void reset();
    // CPU-visible read path through cartridge, RAM, PPU, APU, and controller registers.
    u8 cpuRead(u16 address);
    // CPU-visible write path through cartridge, RAM, PPU, APU, DMA, and controllers.
    void cpuWrite(u16 address, u8 data);
    // Clock one PPU cycle.
    void clock();
    // Clock CPU-rate devices: mapper CPU logic and APU sample generation.
    void clockCpuDevices();
    // True while OAM DMA is stalling CPU instruction execution.
    bool cpuStalled() const { return dmaCycles_ > 0; }
    // Consume one DMA stall cycle while PPU/APU continue running.
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
