#pragma once

#include "../Mapper.hpp"

namespace nes {

class Mmc1Mapper final : public Mapper {
public:
    Mmc1Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring);

    bool cpuMapRead(u16 address, u32& mapped, u8& data) override;
    bool cpuMapWrite(u16 address, u32& mapped, u8 data) override;
    bool ppuMapRead(u16 address, u32& mapped) override;
    bool ppuMapWrite(u16 address, u32& mapped) override;
    void reset() override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    void commit(u16 address, u8 value);

    int prgBanks_ = 0;
    int chrBanks_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
    u8 shift_ = 0x10;
    u8 control_ = 0x1c;
    u8 chrBank0_ = 0;
    u8 chrBank1_ = 0;
    u8 prgBank_ = 0;
};

} // namespace nes
