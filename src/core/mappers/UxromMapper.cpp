#include "UxromMapper.hpp"

#include <algorithm>

namespace nes {

UxromMapper::UxromMapper(int prgBanks, int chrBanks, Mirroring mirroring)
    : prgBanks_(prgBanks), chrBanks_(chrBanks), mirroring_(mirroring) {
    reset();
}

void UxromMapper::reset() {
    selectedBank_ = 0;
}

bool UxromMapper::cpuMapRead(u16 address, u32& mapped, u8&) {
    if (address < 0x8000) {
        return false;
    }

    const u32 bankCount = static_cast<u32>(std::max(1, prgBanks_));
    const u32 bank = address < 0xc000 ? selectedBank_ % bankCount : bankCount - 1;
    mapped = bank * 0x4000 + (address & 0x3fff);
    return true;
}

bool UxromMapper::cpuMapWrite(u16 address, u32&, u8 data) {
    if (address < 0x8000) {
        return false;
    }

    selectedBank_ = data;
    return false;
}

bool UxromMapper::ppuMapRead(u16 address, u32& mapped) {
    if (address >= 0x2000) {
        return false;
    }
    mapped = address;
    return true;
}

bool UxromMapper::ppuMapWrite(u16 address, u32& mapped) {
    if (address >= 0x2000 || chrBanks_ != 0) {
        return false;
    }
    mapped = address;
    return true;
}

} // namespace nes
