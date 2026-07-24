#pragma once

#include "../Mapper.hpp"

namespace nes {

// Mapper 0: fixed PRG mapping and direct CHR access, used by simple early games.
class NromMapper final : public Mapper {
public:
    NromMapper(int prgBanks, int chrBanks, Mirroring mirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped, u8& data) override;
    bool ppuMapWrite(u16 address, u32& mapped, u8 data) override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
};

} // namespace nes
