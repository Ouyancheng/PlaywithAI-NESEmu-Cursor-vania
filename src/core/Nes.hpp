#pragma once

#include "Bus.hpp"
#include "CPU6502.hpp"

#include <filesystem>
#include <mutex>
#include <memory>

namespace nes {

class Nes {
public:
    // Load an iNES ROM, create its mapper, connect all devices, and reset the machine.
    bool loadRom(const std::filesystem::path& path, std::string& error);
    // Reset CPU, PPU, APU, bus, and mapper state for the currently loaded cartridge.
    void reset();
    // Advance emulation until the PPU publishes one complete frame.
    void stepFrame();
    // Update frontend-controlled controller button state.
    void setButton(int controller, Button button, bool pressed);
    // Return the latest completed framebuffer. Safe when caller is synchronized with emulation.
    const Framebuffer& framebuffer() const { return bus_.ppu().framebuffer(); }
    // Return a locked framebuffer copy for frontends that render on another thread/timer.
    Framebuffer framebufferSnapshot() const;
    // Move generated audio samples out of the APU for the frontend audio queue.
    std::vector<float> takeAudioSamples() { return bus_.apu().takeSamples(); }
    // Human-readable mapper name for window titles and diagnostics.
    std::string mapperName() const;
    // True after a ROM has been loaded successfully.
    bool hasCartridge() const { return cartridge_ != nullptr; }

private:
    void resetUnlocked();

    Bus bus_;
    CPU6502 cpu_;
    std::unique_ptr<Cartridge> cartridge_;
    mutable std::mutex mutex_;
    bool nmiPending_ = false;
    bool irqPending_ = false;
};

} // namespace nes
