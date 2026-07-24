#include "Mmc3Mapper.hpp"

#include <algorithm>

namespace nes {

Mmc3Mapper::Mmc3Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring)
    : prgBanks_(prgBanks),
      chrBanks_(chrBanks),
      mirroring_(defaultMirroring),
      fixedMirroring_(defaultMirroring == Mirroring::FourScreen) {
    reset();
}

void Mmc3Mapper::reset() {
    regs_.fill(0);
    prgRamEnabled_ = true;
    prgRamWriteEnabled_ = true;
    bankSelect_ = 0;
    irqLatch_ = 0;
    irqCounter_ = 0;
    irqEnabled_ = false;
    irqReload_ = false;
    irqPending_ = false;
    prgMode_ = false;
    chrMode_ = false;
}

bool Mmc3Mapper::cpuMapRead(u16 address, u32& mapped, u8& data) {
    if (address >= 0x6000 && address <= 0x7fff) {
        data = prgRamEnabled_ ? prgRam_[address & 0x1fff] : 0xff;
        mapped = kMapperHandled;
        return true;
    }
    if (address < 0x8000) {
        return false;
    }
    const u32 last8k = static_cast<u32>(std::max(1, prgBanks_) * 2 - 1);
    const u32 secondLast8k = last8k > 0 ? last8k - 1 : 0;
    auto bankOffset = [&](u32 bank) { return (bank % (last8k + 1)) * 0x2000 + (address & 0x1fff); };

    if (address <= 0x9fff) {
        mapped = bankOffset(prgMode_ ? secondLast8k : regs_[6]);
    } else if (address <= 0xbfff) {
        mapped = bankOffset(regs_[7]);
    } else if (address <= 0xdfff) {
        mapped = bankOffset(prgMode_ ? regs_[6] : secondLast8k);
    } else {
        mapped = bankOffset(last8k);
    }
    return true;
}

bool Mmc3Mapper::cpuMapWrite(u16 address, u32& mapped, u8 data) {
    if (address >= 0x6000 && address <= 0x7fff) {
        if (prgRamEnabled_ && prgRamWriteEnabled_) {
            prgRam_[address & 0x1fff] = data;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address < 0x8000) {
        return false;
    }
    const bool even = (address & 1u) == 0;
    if (address <= 0x9fff) {
        if (even) {
            bankSelect_ = data & 0x07;
            prgMode_ = (data & 0x40) != 0;
            chrMode_ = (data & 0x80) != 0;
        } else {
            regs_[bankSelect_] = data;
        }
    } else if (address <= 0xbfff) {
        if (even) {
            if (!fixedMirroring_) {
                mirroring_ = (data & 1u) ? Mirroring::Horizontal : Mirroring::Vertical;
            }
        } else {
            prgRamEnabled_ = (data & 0x80) != 0;
            prgRamWriteEnabled_ = (data & 0x40) == 0;
        }
    } else if (address <= 0xdfff) {
        if (even) {
            irqLatch_ = data;
        } else {
            irqReload_ = true;
            irqCounter_ = 0;
        }
    } else {
        irqEnabled_ = !even;
        if (even) {
            irqPending_ = false;
        }
    }
    return false;
}

bool Mmc3Mapper::ppuMapRead(u16 address, u32& mapped) {
    if (address >= 0x2000) {
        return false;
    }
    const u32 chrCount1k = static_cast<u32>(std::max(1, chrBanks_) * 8);
    auto page = [&](u8 reg, u16 offset) {
        return (static_cast<u32>(reg) % chrCount1k) * 0x0400 + offset;
    };

    const u16 a = address & 0x1fff;
    if (!chrMode_) {
        if (a < 0x0800) mapped = page(regs_[0] & 0xfe, a & 0x07ff);
        else if (a < 0x1000) mapped = page(regs_[1] & 0xfe, a & 0x07ff);
        else if (a < 0x1400) mapped = page(regs_[2], a & 0x03ff);
        else if (a < 0x1800) mapped = page(regs_[3], a & 0x03ff);
        else if (a < 0x1c00) mapped = page(regs_[4], a & 0x03ff);
        else mapped = page(regs_[5], a & 0x03ff);
    } else {
        if (a < 0x0400) mapped = page(regs_[2], a & 0x03ff);
        else if (a < 0x0800) mapped = page(regs_[3], a & 0x03ff);
        else if (a < 0x0c00) mapped = page(regs_[4], a & 0x03ff);
        else if (a < 0x1000) mapped = page(regs_[5], a & 0x03ff);
        else if (a < 0x1800) mapped = page(regs_[0] & 0xfe, a & 0x07ff);
        else mapped = page(regs_[1] & 0xfe, a & 0x07ff);
    }
    return true;
}

bool Mmc3Mapper::ppuMapWrite(u16 address, u32& mapped) {
    if (address >= 0x2000 || chrBanks_ != 0) {
        return false;
    }
    mapped = address;
    return true;
}

void Mmc3Mapper::scanline() {
    if (irqCounter_ == 0 || irqReload_) {
        irqCounter_ = irqLatch_;
        irqReload_ = false;
    } else {
        --irqCounter_;
    }
    if (irqCounter_ == 0 && irqEnabled_) {
        irqPending_ = true;
    }
}

} // namespace nes
