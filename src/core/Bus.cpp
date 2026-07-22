#include "Bus.hpp"

namespace nes {

void Bus::insertCartridge(Cartridge* cartridge) {
    cartridge_ = cartridge;
    ppu_.connect(cartridge_);
}

void Bus::reset() {
    ram_.fill(0);
    ppu_.reset();
    apu_.reset();
    dmaCycles_ = 0;
    if (cartridge_) {
        cartridge_->reset();
    }
}

u8 Bus::cpuRead(u16 address) {
    u8 data = 0;
    if (cartridge_ && cartridge_->cpuRead(address, data)) {
        return data;
    }
    if (address <= 0x1fff) {
        return ram_[address & 0x07ff];
    }
    if (address <= 0x3fff) {
        return ppu_.cpuRead(address & 0x0007);
    }
    if (address == 0x4015) {
        return apu_.cpuRead(address);
    }
    if (address == 0x4016 || address == 0x4017) {
        return controllers_[address & 1].read();
    }
    return data;
}

void Bus::cpuWrite(u16 address, u8 data) {
    if (cartridge_ && cartridge_->cpuWrite(address, data)) {
        return;
    }
    if (address <= 0x1fff) {
        ram_[address & 0x07ff] = data;
    } else if (address <= 0x3fff) {
        ppu_.cpuWrite(address & 0x0007, data);
    } else if (address == 0x4014) {
        const u16 base = static_cast<u16>(data << 8);
        for (u16 i = 0; i < 256; ++i) {
            ppu_.dmaWrite(static_cast<u8>(i), cpuRead(static_cast<u16>(base + i)));
        }
        dmaCycles_ = 513;
    } else if ((address >= 0x4000 && address <= 0x4015) || address == 0x4017) {
        apu_.cpuWrite(address, data);
    } else if (address == 0x4016) {
        controllers_[0].strobe(data);
        controllers_[1].strobe(data);
    }
}

void Bus::clock() {
    ppu_.clock();
}

void Bus::clockCpuDevices() {
    if (cartridge_) {
        cartridge_->clockCpu();
        apu_.setExpansionAudio(cartridge_->expansionAudioSample());
    } else {
        apu_.setExpansionAudio(0);
    }
    apu_.clock();
}

void Bus::tickCpuDmaStall() {
    if (dmaCycles_ > 0) {
        --dmaCycles_;
    }
}

} // namespace nes
