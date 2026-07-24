#pragma once

#include "../Mapper.hpp"

namespace nes {

// Mapper 2: switchable 16 KB PRG bank at $8000 and fixed last bank at $C000.
class UxromMapper final : public Mapper {
public:
    UxromMapper(int prgBanks, int chrBanks, Mirroring mirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped, u8& data) override;
    bool ppuMapWrite(u16 address, u32& mapped, u8 data) override;
    void reset() override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
    u8 selectedBank_ = 0;
};

} // namespace nes
