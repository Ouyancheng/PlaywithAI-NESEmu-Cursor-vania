#pragma once

#include "Bus.hpp"
#include "CPU6502.hpp"

#include <filesystem>
#include <mutex>
#include <memory>

namespace nes {

class Nes {
public:
    bool loadRom(const std::filesystem::path& path, std::string& error);
    void reset();
    void stepFrame();
    void setButton(int controller, Button button, bool pressed);
    const Framebuffer& framebuffer() const { return bus_.ppu().framebuffer(); }
    Framebuffer framebufferSnapshot() const;
    std::vector<float> takeAudioSamples() { return bus_.apu().takeSamples(); }
    std::string mapperName() const;
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
