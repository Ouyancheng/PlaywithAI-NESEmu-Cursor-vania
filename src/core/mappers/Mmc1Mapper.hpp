#pragma once

#include "../Mapper.hpp"

#include <array>

namespace nes {

// Mapper 1: serial shift-register mapper with PRG/CHR banking and runtime mirroring.
class Mmc1Mapper final : public Mapper {
public:
    Mmc1Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped, u8& data) override;
    bool ppuMapWrite(u16 address, u32& mapped, u8 data) override;
    void reset() override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    // Commit the five-bit serial register to the target MMC1 register selected by address.
    void commit(u16 address, u8 value);
    // PRG helpers keep large-ROM bank selection and fixed-bank mirroring in one place.
    u32 prgBankCount() const;
    u32 prgBankWindow(u32 bank) const;
    u32 fixedLastPrgBank() const;

    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::array<u8, 8192> prgRam_{};
    bool prgRamEnabled_ = true;
    u8 shift_ = 0x10;
    u8 control_ = 0x1c;
    u8 chrBank0_ = 0;
    u8 chrBank1_ = 0;
    u8 prgBank_ = 0;
};

} // namespace nes
