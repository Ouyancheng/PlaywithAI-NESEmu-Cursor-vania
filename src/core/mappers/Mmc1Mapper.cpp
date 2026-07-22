#include "Mmc1Mapper.hpp"

#include <algorithm>

namespace nes {

Mmc1Mapper::Mmc1Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring)
    : prgBanks_(prgBanks), chrBanks_(chrBanks), mirroring_(defaultMirroring) {
    reset();
}

void Mmc1Mapper::reset() {
    shift_ = 0x10;
    control_ = 0x1c;
    chrBank0_ = 0;
    chrBank1_ = 0;
    prgBank_ = 0;
}

void Mmc1Mapper::commit(u16 address, u8 value) {
    if (address <= 0x9fff) {
        control_ = value;
        switch (control_ & 0x03) {
        case 0: mirroring_ = Mirroring::SingleScreenLower; break;
        case 1: mirroring_ = Mirroring::SingleScreenUpper; break;
        case 2: mirroring_ = Mirroring::Vertical; break;
        case 3: mirroring_ = Mirroring::Horizontal; break;
        }
    } else if (address <= 0xbfff) {
        chrBank0_ = value;
    } else if (address <= 0xdfff) {
        chrBank1_ = value;
    } else {
        prgBank_ = value & 0x0f;
    }
}

bool Mmc1Mapper::cpuMapRead(u16 address, u32& mapped, u8&) {
    if (address < 0x8000) {
        return false;
    }
    const u8 mode = (control_ >> 2) & 0x03;
    const u32 bankCount = static_cast<u32>(std::max(1, prgBanks_));
    if (mode <= 1) {
        const u32 bank = (prgBank_ & 0x0e) % bankCount;
        mapped = bank * 0x4000 + (address & 0x7fff);
    } else if (mode == 2) {
        mapped = (address < 0xc000)
            ? (address & 0x3fff)
            : ((prgBank_ % bankCount) * 0x4000 + (address & 0x3fff));
    } else {
        mapped = (address < 0xc000)
            ? ((prgBank_ % bankCount) * 0x4000 + (address & 0x3fff))
            : ((bankCount - 1) * 0x4000 + (address & 0x3fff));
    }
    return true;
}

bool Mmc1Mapper::cpuMapWrite(u16 address, u32&, u8 data) {
    if (address < 0x8000) {
        return false;
    }
    if (data & 0x80) {
        shift_ = 0x10;
        control_ |= 0x0c;
        return false;
    }
    const bool complete = (shift_ & 1u) != 0;
    shift_ = static_cast<u8>((shift_ >> 1u) | ((data & 1u) << 4u));
    if (complete) {
        commit(address, shift_);
        shift_ = 0x10;
    }
    return false;
}

bool Mmc1Mapper::ppuMapRead(u16 address, u32& mapped) {
    if (address >= 0x2000) {
        return false;
    }
    if (chrBanks_ == 0) {
        mapped = address;
        return true;
    }
    const u32 bankCount4k = static_cast<u32>(chrBanks_ * 2);
    if (control_ & 0x10) {
        const u32 bank = ((address < 0x1000 ? chrBank0_ : chrBank1_) % bankCount4k);
        mapped = bank * 0x1000 + (address & 0x0fff);
    } else {
        const u32 bank = (chrBank0_ & 0x1e) % bankCount4k;
        mapped = bank * 0x1000 + address;
    }
    return true;
}

bool Mmc1Mapper::ppuMapWrite(u16 address, u32& mapped) {
    if (address >= 0x2000 || chrBanks_ != 0) {
        return false;
    }
    mapped = address;
    return true;
}

} // namespace nes
