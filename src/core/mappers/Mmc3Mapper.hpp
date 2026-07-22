#pragma once

#include "../Mapper.hpp"

#include <array>

namespace nes {

class Mmc3Mapper final : public Mapper {
public:
    Mmc3Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped) override;
    bool ppuMapWrite(u16 address, u32& mapped) override;
    void reset() override;
    void scanline() override;
    bool irqPending() const override { return irqPending_; }
    void clearIrq() override { irqPending_ = false; }
    Mirroring mirroring() const override { return mirroring_; }

private:
    std::array<u8, 8> regs_{};
    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
    u8 bankSelect_ = 0;
    u8 irqLatch_ = 0;
    u8 irqCounter_ = 0;
    bool irqEnabled_ = false;
    bool irqReload_ = false;
    bool irqPending_ = false;
    bool prgMode_ = false;
    bool chrMode_ = false;
};

} // namespace nes
