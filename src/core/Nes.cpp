#include "Nes.hpp"

namespace nes {

bool Nes::loadRom(const std::filesystem::path& path, std::string& error) {
    auto cart = Cartridge::loadFromFile(path, error);
    if (!cart) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cartridge_ = std::move(cart);
    bus_.insertCartridge(cartridge_.get());
    cpu_.connect(&bus_);
    resetUnlocked();
    return true;
}

void Nes::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    resetUnlocked();
}

void Nes::resetUnlocked() {
    nmiPending_ = false;
    irqPending_ = false;
    bus_.reset();
    cpu_.reset();
}

void Nes::stepFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cartridge_) {
        return;
    }
    bus_.ppu().clearFrameComplete();
    while (!bus_.ppu().frameComplete()) {
        for (int i = 0; i < 3; ++i) {
            bus_.clock();
        }
        bus_.clockCpuDevices();
        if (bus_.ppu().pollNmi()) {
            nmiPending_ = true;
        }
        irqPending_ = cartridge_->irqPending();
        if (bus_.cpuStalled()) {
            bus_.tickCpuDmaStall();
        } else {
            if (cpu_.complete()) {
                if (nmiPending_) {
                    nmiPending_ = false;
                    cpu_.nmi();
                } else if (irqPending_) {
                    irqPending_ = false;
                    cpu_.irq();
                }
            }
            cpu_.clock();
        }
    }
}

void Nes::setButton(int controller, Button button, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (controller >= 0 && controller < 2) {
        bus_.controller(controller).set(button, pressed);
    }
}

Framebuffer Nes::framebufferSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bus_.ppu().framebuffer();
}

std::string Nes::mapperName() const {
    return cartridge_ ? cartridge_->mapperName() : "No cartridge";
}

} // namespace nes
