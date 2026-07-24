#pragma once

#include "../Mapper.hpp"

#include <array>

namespace nes {

class Vrc6Mapper final : public Mapper {
public:
    Vrc6Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring, bool swapAddressLines);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped, u8& data) override;
    bool ppuMapWrite(u16 address, u32& mapped, u8 data) override;
    void reset() override;
    void clockCpu() override;
    bool irqPending() const override { return irqPending_; }
    void clearIrq() override { irqPending_ = false; }
    u8 expansionAudioSample() override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Vertical;
    std::array<u8, 8192> prgRam_{};
    u8 ppuBankingMode_ = 0;
    bool prgRamEnabled_ = false;
    bool swapAddressLines_ = false;
    u8 prg16_ = 0;
    u8 prg8_ = 0;
    u8 chr1k_[8]{};
    u8 irqLatch_ = 0;
    u8 irqCounter_ = 0;
    int irqPrescaler_ = 0;
    bool irqEnabled_ = false;
    bool irqEnableAfterAck_ = false;
    bool irqCycleMode_ = false;
    bool irqPending_ = false;

    struct Pulse {
        u8 volume = 0;
        u8 duty = 0;
        bool constant = false;
        bool enabled = false;
        u16 period = 1;
        u16 counter = 1;
        u8 phase = 0;
    };

    struct Saw {
        u8 rate = 0;
        bool enabled = false;
        u16 period = 1;
        u16 counter = 1;
        u8 step = 0;
        u8 accumulator = 0;
    };

    Pulse pulse_[2];
    Saw saw_;

    void writePulseRegister(Pulse& pulse, u16 reg, u8 data);
    u8 patternRegisterForAddress(u16 address) const;
    bool patternUsesAddressLsb(u16 address) const;
    u8 nametableRegisterForAddress(u16 address, bool& forceLsb, u8& forcedLsb) const;
    u32 mappedChrAddress(u8 bankRegister, u16 address, bool forceLsb, u8 forcedLsb, bool applyAddressLsb) const;
    u16 decodeRegister(u16 address) const;
};

} // namespace nes
