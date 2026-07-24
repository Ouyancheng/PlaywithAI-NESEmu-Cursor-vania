#include "Mmc5Mapper.hpp"

#include <algorithm>

namespace nes {

Mmc5Mapper::Mmc5Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring)
    : prgBanks_(prgBanks), chrBanks_(chrBanks), defaultMirroring_(defaultMirroring) {
    reset();
}

void Mmc5Mapper::reset() {
    prgMode_ = 3;
    chrMode_ = 3;
    exRamMode_ = 0;
    mirroringReg_ = defaultMirroring_ == Mirroring::Horizontal ? 0x50 : 0x44;
    fillTile_ = 0;
    fillAttr_ = 0;
    prgRamBank_ = 0;
    prgRegs_ = {0x80, 0x80, 0x80, 0xff};
    chrA_.fill(0);
    chrB_.fill(0);
    chrHigh_ = 0;
    fetchKind_ = PpuFetchKind::SpritePattern;
    protect0_ = 0;
    protect1_ = 0;
    irqCompare_ = 0;
    irqCounter_ = 0;
    irqEnabled_ = false;
    irqPending_ = false;
    inFrame_ = false;
    multiplierA_ = 0;
    multiplierB_ = 0;
    pulse_[0] = {};
    pulse_[1] = {};
    pcmLevel_ = 0;
    pcmIrqEnabled_ = false;
    pcmIrqPending_ = false;
}

bool Mmc5Mapper::cpuMapRead(u16 address, u32& mapped, u8& data) {
    if (address >= 0x5000 && address <= 0x5015) {
        if (address == 0x5010) {
            data = static_cast<u8>((pcmIrqEnabled_ && pcmIrqPending_) ? 0x80 : 0x00);
            pcmIrqPending_ = false;
        } else if (address == 0x5015) {
            data = static_cast<u8>((pulse_[0].enabled ? 0x01 : 0x00) | (pulse_[1].enabled ? 0x02 : 0x00));
        } else {
            data = 0;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x5200 && address <= 0x5206) {
        if (address == 0x5204) {
            data = static_cast<u8>((irqPending_ ? 0x80 : 0x00) | (inFrame_ ? 0x40 : 0x00));
            irqPending_ = false;
        } else if (address == 0x5205) {
            data = static_cast<u8>((multiplierA_ * multiplierB_) & 0xff);
        } else if (address == 0x5206) {
            data = static_cast<u8>((multiplierA_ * multiplierB_) >> 8);
        } else {
            data = 0;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x5c00 && address <= 0x5fff) {
        data = exRam_[address & 0x03ff];
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x6000 && address <= 0x7fff) {
        data = readWram(prgRamBank_, address & 0x1fff);
        mapped = kMapperHandled;
        return true;
    }
    if (address < 0x8000) {
        return false;
    }

    switch (prgMode_ & 0x03) {
    case 0:
        return readPrgWindow(address, prgRegs_[3], 0x8000, 0x8000, false, data, mapped);
    case 1:
        return address < 0xc000
            ? readPrgWindow(address, prgRegs_[1], 0x4000, 0x8000, true, data, mapped)
            : readPrgWindow(address, prgRegs_[3], 0x4000, 0xc000, false, data, mapped);
    case 2:
        if (address < 0xc000) {
            return readPrgWindow(address, prgRegs_[1], 0x4000, 0x8000, true, data, mapped);
        }
        return address < 0xe000
            ? readPrgWindow(address, prgRegs_[2], 0x2000, 0xc000, true, data, mapped)
            : readPrgWindow(address, prgRegs_[3], 0x2000, 0xe000, false, data, mapped);
    case 3:
    default: {
        const u8 reg = prgRegs_[(address - 0x8000) / 0x2000];
        const u16 base = static_cast<u16>(address & 0xe000);
        return readPrgWindow(address, reg, 0x2000, base, base < 0xe000, data, mapped);
    }
    }
}

bool Mmc5Mapper::cpuMapWrite(u16 address, u32& mapped, u8 data) {
    if (address >= 0x5000 && address <= 0x5015) {
        if (address <= 0x5007) {
            writePulseRegister(pulse_[(address >> 2) & 1], address & 0x03, data);
        } else if (address == 0x5010) {
            pcmIrqEnabled_ = (data & 0x80) != 0;
        } else if (address == 0x5011) {
            if (data == 0) {
                pcmIrqPending_ = true;
            } else {
                pcmLevel_ = data;
                pcmIrqPending_ = false;
            }
        } else if (address == 0x5015) {
            pulse_[0].enabled = (data & 0x01) != 0;
            pulse_[1].enabled = (data & 0x02) != 0;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x5100 && address <= 0x5130) {
        switch (address) {
        case 0x5100: prgMode_ = data & 0x03; break;
        case 0x5101: chrMode_ = data & 0x03; break;
        case 0x5102: protect0_ = data & 0x03; break;
        case 0x5103: protect1_ = data & 0x03; break;
        case 0x5104: exRamMode_ = data & 0x03; break;
        case 0x5105: mirroringReg_ = data; break;
        case 0x5106: fillTile_ = data; break;
        case 0x5107: fillAttr_ = data & 0x03; break;
        case 0x5113: prgRamBank_ = data & 0x0f; break;
        case 0x5114:
        case 0x5115:
        case 0x5116:
        case 0x5117:
            prgRegs_[address - 0x5114] = data;
            break;
        case 0x5130:
            chrHigh_ = data & 0x03;
            break;
        default:
            if (address >= 0x5120 && address <= 0x5127) {
                chrA_[address - 0x5120] = data;
            } else if (address >= 0x5128 && address <= 0x512b) {
                chrB_[address - 0x5128] = data;
            }
            break;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x5200 && address <= 0x5206) {
        switch (address) {
        case 0x5203:
            irqCompare_ = data;
            break;
        case 0x5204:
            irqEnabled_ = (data & 0x80) != 0;
            if (!irqEnabled_) {
                irqPending_ = false;
            }
            break;
        case 0x5205:
            multiplierA_ = data;
            break;
        case 0x5206:
            multiplierB_ = data;
            break;
        default:
            break;
        }
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x5c00 && address <= 0x5fff) {
        exRam_[address & 0x03ff] = data;
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x6000 && address <= 0x7fff) {
        writeWram(prgRamBank_, address & 0x1fff, data);
        mapped = kMapperHandled;
        return true;
    }
    if (address < 0x8000) {
        return false;
    }

    switch (prgMode_ & 0x03) {
    case 0:
        return writePrgWindow(address, prgRegs_[3], 0x8000, 0x8000, false, data, mapped);
    case 1:
        return address < 0xc000
            ? writePrgWindow(address, prgRegs_[1], 0x4000, 0x8000, true, data, mapped)
            : writePrgWindow(address, prgRegs_[3], 0x4000, 0xc000, false, data, mapped);
    case 2:
        if (address < 0xc000) {
            return writePrgWindow(address, prgRegs_[1], 0x4000, 0x8000, true, data, mapped);
        }
        return address < 0xe000
            ? writePrgWindow(address, prgRegs_[2], 0x2000, 0xc000, true, data, mapped)
            : writePrgWindow(address, prgRegs_[3], 0x2000, 0xe000, false, data, mapped);
    case 3:
    default: {
        const u8 reg = prgRegs_[(address - 0x8000) / 0x2000];
        const u16 base = static_cast<u16>(address & 0xe000);
        return writePrgWindow(address, reg, 0x2000, base, base < 0xe000, data, mapped);
    }
    }
}

bool Mmc5Mapper::ppuMapRead(u16 address, u32& mapped, u8& data) {
    if (address < 0x2000) {
        const u8 page = static_cast<u8>((address >> 10) & 0x07);
        const u32 chrCount1k = static_cast<u32>(std::max(1, chrBanks_) * 8);
        const u32 bank = chrBankForPage(page) % chrCount1k;
        mapped = bank * 0x0400 + (address & 0x03ff);
        return true;
    }
    if (address < 0x3000) {
        data = readNametable(address);
        mapped = kMapperHandled;
        return true;
    }
    return false;
}

bool Mmc5Mapper::ppuMapWrite(u16 address, u32& mapped, u8 data) {
    if (address < 0x2000) {
        if (chrBanks_ != 0) {
            return false;
        }
        mapped = address;
        return true;
    }
    if (address < 0x3000) {
        writeNametable(address, data);
        mapped = kMapperHandled;
        return true;
    }
    return false;
}

void Mmc5Mapper::clockCpu() {
    for (auto& p : pulse_) {
        if (p.enabled && p.counter-- == 0) {
            p.counter = std::max<u16>(1, p.period);
            p.phase = static_cast<u8>((p.phase + 1) & 0x07);
        }
    }
}

void Mmc5Mapper::scanlineStart(int scanline) {
    inFrame_ = true;
    irqCounter_ = static_cast<u8>(scanline);
    if (irqCompare_ != 0 && irqCounter_ == irqCompare_) {
        irqPending_ = true;
    }
}

u8 Mmc5Mapper::expansionAudioSample() {
    int mix = pcmLevel_ >> 3;
    static constexpr u8 kDutyThreshold[4] = {1, 2, 4, 6};
    for (const auto& p : pulse_) {
        if (p.enabled && p.phase < kDutyThreshold[p.duty & 0x03]) {
            mix += p.volume;
        }
    }
    return static_cast<u8>(std::min(63, mix));
}

Mirroring Mmc5Mapper::mirroring() const {
    if (defaultMirroring_ == Mirroring::FourScreen) {
        return Mirroring::FourScreen;
    }
    switch (mirroringReg_) {
    case 0x00: return Mirroring::SingleScreenLower;
    case 0x55: return Mirroring::SingleScreenUpper;
    case 0x44: return Mirroring::Vertical;
    case 0x50: return Mirroring::Horizontal;
    default: return defaultMirroring_;
    }
}

bool Mmc5Mapper::wramWriteEnabled() const {
    return protect0_ == 0x02 && protect1_ == 0x01;
}

u8 Mmc5Mapper::readWram(u8 bank, u16 offset) const {
    const u32 mapped = (static_cast<u32>(bank & 0x0f) * 0x2000 + (offset & 0x1fff)) % wram_.size();
    return wram_[mapped];
}

void Mmc5Mapper::writeWram(u8 bank, u16 offset, u8 data) {
    if (!wramWriteEnabled()) {
        return;
    }
    const u32 mapped = (static_cast<u32>(bank & 0x0f) * 0x2000 + (offset & 0x1fff)) % wram_.size();
    wram_[mapped] = data;
}

bool Mmc5Mapper::readPrgWindow(u16 address, u8 reg, u32 windowSize, u16 windowBase, bool allowRam, u8& data, u32& mapped) const {
    const u32 offset = static_cast<u32>(address - windowBase);
    if (allowRam && (reg & 0x80) == 0) {
        data = readWram(reg, static_cast<u16>(offset));
        mapped = kMapperHandled;
        return true;
    }

    const u32 bankCount8k = static_cast<u32>(std::max(1, prgBanks_) * 2);
    u32 bank = reg & 0x7f;
    if (windowSize == 0x8000) {
        bank = (bank & 0x7c) + (offset / 0x2000);
    } else if (windowSize == 0x4000) {
        bank = (bank & 0x7e) + (offset / 0x2000);
    }
    mapped = (bank % bankCount8k) * 0x2000 + (offset & 0x1fff);
    return true;
}

bool Mmc5Mapper::writePrgWindow(u16 address, u8 reg, u32 windowSize, u16 windowBase, bool allowRam, u8 data, u32& mapped) {
    (void)windowSize;
    const u32 offset = static_cast<u32>(address - windowBase);
    if (allowRam && (reg & 0x80) == 0) {
        writeWram(reg, static_cast<u16>(offset), data);
        mapped = kMapperHandled;
        return true;
    }
    return false;
}

u32 Mmc5Mapper::chrBankForPage(u8 page) const {
    auto withHigh = [this](u8 bank) {
        return (static_cast<u32>(chrHigh_ & 0x03) << 8) | bank;
    };
    if (fetchKind_ == PpuFetchKind::BackgroundPattern) {
        const u8 bgPage = page & 0x03;
        switch (chrMode_ & 0x03) {
        case 0:
        case 1:
            return withHigh(static_cast<u8>((chrB_[3] & 0xfc) + bgPage));
        case 2:
            return withHigh(static_cast<u8>((chrB_[(bgPage & 0x02) | 1] & 0xfe) + (bgPage & 1)));
        case 3:
        default:
            return withHigh(chrB_[bgPage]);
        }
    }
    switch (chrMode_ & 0x03) {
    case 0:
        return withHigh(static_cast<u8>((chrA_[7] & 0xf8) + page));
    case 1:
        return page < 4
            ? withHigh(static_cast<u8>((chrA_[3] & 0xfc) + page))
            : withHigh(static_cast<u8>((chrA_[7] & 0xfc) + (page - 4)));
    case 2:
        return withHigh(static_cast<u8>((chrA_[(page & 0x06) | 1] & 0xfe) + (page & 1)));
    case 3:
    default:
        return withHigh(chrA_[page & 0x07]);
    }
}

u8 Mmc5Mapper::nametableSource(u16 address) const {
    const u8 table = static_cast<u8>(((address - 0x2000) >> 10) & 0x03);
    return static_cast<u8>((mirroringReg_ >> (table * 2)) & 0x03);
}

u8 Mmc5Mapper::readNametable(u16 address) const {
    const u16 offset = static_cast<u16>((address - 0x2000) & 0x03ff);
    switch (nametableSource(address)) {
    case 0:
        return nametableRam_[offset];
    case 1:
        return nametableRam_[0x0400 + offset];
    case 2:
        return exRam_[offset];
    case 3:
    default:
        return offset < 0x03c0 ? fillTile_ : fillAttr_;
    }
}

void Mmc5Mapper::writeNametable(u16 address, u8 data) {
    const u16 offset = static_cast<u16>((address - 0x2000) & 0x03ff);
    switch (nametableSource(address)) {
    case 0:
        nametableRam_[offset] = data;
        break;
    case 1:
        nametableRam_[0x0400 + offset] = data;
        break;
    case 2:
        exRam_[offset] = data;
        break;
    default:
        break;
    }
}

void Mmc5Mapper::writePulseRegister(Pulse& pulse, u16 reg, u8 data) {
    switch (reg) {
    case 0:
        pulse.volume = data & 0x0f;
        pulse.duty = static_cast<u8>((data >> 6) & 0x03);
        break;
    case 2:
        pulse.period = static_cast<u16>((pulse.period & 0x0700) | data);
        break;
    case 3:
        pulse.period = static_cast<u16>(((data & 0x07) << 8) | (pulse.period & 0x00ff));
        pulse.phase = 0;
        break;
    default:
        break;
    }
}

} // namespace nes
