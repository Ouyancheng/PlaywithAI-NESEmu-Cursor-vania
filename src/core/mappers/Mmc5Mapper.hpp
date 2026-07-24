#pragma once

#include "../Mapper.hpp"

#include <array>

namespace nes {

class Mmc5Mapper final : public Mapper {
public:
    Mmc5Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped, u8& data) override;
    bool ppuMapWrite(u16 address, u32& mapped, u8 data) override;
    void reset() override;
    void clockCpu() override;
    void scanline() override {}
    void scanlineStart(int scanline) override;
    bool usesPpuFetchNotifications() const override { return true; }
    void notifyPpuFetch(PpuFetchKind kind) override { fetchKind_ = kind; }
    bool irqPending() const override { return irqEnabled_ && irqPending_; }
    void clearIrq() override { irqPending_ = false; }
    u8 expansionAudioSample() override;
    Mirroring mirroring() const override;

private:
    struct Pulse {
        u8 volume = 0;
        u8 duty = 0;
        bool enabled = false;
        u16 period = 1;
        u16 counter = 1;
        u8 phase = 0;
    };

    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring defaultMirroring_ = Mirroring::Horizontal;
    std::array<u8, 128 * 1024> wram_{};
    std::array<u8, 1024> exRam_{};
    std::array<u8, 2048> nametableRam_{};

    u8 prgMode_ = 3;
    u8 chrMode_ = 3;
    u8 exRamMode_ = 0;
    u8 mirroringReg_ = 0x44;
    u8 fillTile_ = 0;
    u8 fillAttr_ = 0;
    u8 prgRamBank_ = 0;
    std::array<u8, 4> prgRegs_{{0x80, 0x80, 0x80, 0xff}};
    std::array<u8, 8> chrA_{};
    std::array<u8, 4> chrB_{};
    u8 chrHigh_ = 0;
    PpuFetchKind fetchKind_ = PpuFetchKind::SpritePattern;
    u8 protect0_ = 0;
    u8 protect1_ = 0;

    u8 irqCompare_ = 0;
    u8 irqCounter_ = 0;
    bool irqEnabled_ = false;
    bool irqPending_ = false;
    bool inFrame_ = false;

    u8 multiplierA_ = 0;
    u8 multiplierB_ = 0;

    Pulse pulse_[2];
    u8 pcmLevel_ = 0;
    bool pcmIrqEnabled_ = false;
    bool pcmIrqPending_ = false;

    bool wramWriteEnabled() const;
    u8 readWram(u8 bank, u16 offset) const;
    void writeWram(u8 bank, u16 offset, u8 data);
    bool readPrgWindow(u16 address, u8 reg, u32 windowSize, u16 windowBase, bool allowRam, u8& data, u32& mapped) const;
    bool writePrgWindow(u16 address, u8 reg, u32 windowSize, u16 windowBase, bool allowRam, u8 data, u32& mapped);
    u32 chrBankForPage(u8 page) const;
    u8 nametableSource(u16 address) const;
    u8 readNametable(u16 address) const;
    void writeNametable(u16 address, u8 data);
    void writePulseRegister(Pulse& pulse, u16 reg, u8 data);
};

} // namespace nes
