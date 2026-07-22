#include "NromMapper.hpp"

namespace nes {

NromMapper::NromMapper(int prgBanks, int chrBanks, Mirroring mirroring)
    : prgBanks_(prgBanks), chrBanks_(chrBanks), mirroring_(mirroring) {}

bool NromMapper::cpuMapRead(u16 address, u32& mapped, u8&) {
    if (address < 0x8000) {
        return false;
    }
    mapped = address & (prgBanks_ > 1 ? 0x7fff : 0x3fff);
    return true;
}

bool NromMapper::cpuMapWrite(u16 address, u32& mapped, u8) {
    if (address < 0x8000) {
        return false;
    }
    mapped = address & (prgBanks_ > 1 ? 0x7fff : 0x3fff);
    return true;
}

bool NromMapper::ppuMapRead(u16 address, u32& mapped) {
    if (address >= 0x2000) {
        return false;
    }
    mapped = address;
    return true;
}

bool NromMapper::ppuMapWrite(u16 address, u32& mapped) {
    if (address >= 0x2000 || chrBanks_ != 0) {
        return false;
    }
    mapped = address;
    return true;
}

} // namespace nes
