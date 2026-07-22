#include "Vrc6Mapper.hpp"

#include <algorithm>

namespace nes {

Vrc6Mapper::Vrc6Mapper(int prgBanks, int chrBanks, Mirroring defaultMirroring, bool swapAddressLines)
    : prgBanks_(prgBanks), chrBanks_(chrBanks), mirroring_(defaultMirroring), swapAddressLines_(swapAddressLines) {
    reset();
}

void Vrc6Mapper::reset() {
    prgRam_.fill(0);
    prg16_ = 0;
    prg8_ = 0;
    ppuBankingMode_ = 0;
    prgRamEnabled_ = false;
    std::fill(std::begin(chr1k_), std::end(chr1k_), 0);
    irqLatch_ = 0;
    irqCounter_ = 0;
    irqPrescaler_ = 341;
    irqEnabled_ = false;
    irqEnableAfterAck_ = false;
    irqCycleMode_ = false;
    irqPending_ = false;
    pulse_[0] = {};
    pulse_[1] = {};
    saw_ = {};
}

bool Vrc6Mapper::cpuMapRead(u16 address, u32& mapped, u8& data) {
    const u32 bankCount8k = static_cast<u32>(std::max(1, prgBanks_) * 2);
    if (address >= 0x6000 && address <= 0x7fff) {
        data = prgRamEnabled_ ? prgRam_[address & 0x1fff] : 0xff;
        mapped = kMapperHandled;
        return true;
    }
    if (address >= 0x8000 && address <= 0xbfff) {
        mapped = (static_cast<u32>(prg16_) % (bankCount8k / 2)) * 0x4000 + (address & 0x3fff);
        return true;
    }
    if (address >= 0xc000 && address <= 0xdfff) {
        mapped = (static_cast<u32>(prg8_) % bankCount8k) * 0x2000 + (address & 0x1fff);
        return true;
    }
    if (address >= 0xe000) {
        mapped = (bankCount8k - 1) * 0x2000 + (address & 0x1fff);
        return true;
    }
    return false;
}

bool Vrc6Mapper::cpuMapWrite(u16 address, u32& mapped, u8 data) {
    const u16 reg = decodeRegister(address);
    if (address >= 0x6000 && address <= 0x7fff) {
        if (prgRamEnabled_) {
            prgRam_[address & 0x1fff] = data;
        }
        mapped = kMapperHandled;
        return true;
    }

    switch (reg & 0xf003) {
    case 0x8000:
        prg16_ = data & 0x0f;
        break;
    case 0x9000:
    case 0x9001:
    case 0x9002:
        writePulseRegister(pulse_[0], reg & 0x0003, data);
        break;
    case 0xa000:
    case 0xa001:
    case 0xa002:
        writePulseRegister(pulse_[1], reg & 0x0003, data);
        break;
    case 0xb000:
        saw_.rate = data & 0x3f;
        break;
    case 0xb001:
        saw_.period = static_cast<u16>((saw_.period & 0x0f00) | data);
        break;
    case 0xb002:
        saw_.period = static_cast<u16>(((data & 0x0f) << 8) | (saw_.period & 0x00ff));
        saw_.enabled = (data & 0x80) != 0;
        if (!saw_.enabled) {
            saw_.step = 0;
            saw_.accumulator = 0;
        }
        break;
    case 0xb003:
        ppuBankingMode_ = data;
        prgRamEnabled_ = (data & 0x80) != 0;
        switch ((ppuBankingMode_ >> 2) & 0x03) {
        case 0: mirroring_ = Mirroring::Vertical; break;
        case 1: mirroring_ = Mirroring::Horizontal; break;
        case 2: mirroring_ = Mirroring::SingleScreenLower; break;
        case 3: mirroring_ = Mirroring::SingleScreenUpper; break;
        }
        break;
    case 0xc000:
        prg8_ = data & 0x1f;
        break;
    case 0xd000:
    case 0xd001:
    case 0xd002:
    case 0xd003:
        chr1k_[reg & 0x0003] = data;
        break;
    case 0xe000:
    case 0xe001:
    case 0xe002:
    case 0xe003:
        chr1k_[4 + (reg & 0x0003)] = data;
        break;
    case 0xf000:
        irqLatch_ = data;
        break;
    case 0xf001:
        irqEnableAfterAck_ = (data & 0x01) != 0;
        irqEnabled_ = (data & 0x02) != 0;
        irqCycleMode_ = (data & 0x04) != 0;
        if (irqEnabled_) {
            irqCounter_ = irqLatch_;
            irqPrescaler_ = 341;
        }
        irqPending_ = false;
        break;
    case 0xf002:
        irqPending_ = false;
        irqEnabled_ = irqEnableAfterAck_;
        break;
    default:
        break;
    }
    return false;
}

bool Vrc6Mapper::ppuMapRead(u16 address, u32& mapped) {
    const bool romNametables = (ppuBankingMode_ & 0x10) != 0;
    if (address < 0x2000) {
        mapped = mappedChrAddress(patternRegisterForAddress(address), address, false, 0, patternUsesAddressLsb(address));
        return true;
    }
    if (address < 0x3000 && romNametables) {
        bool forceLsb = false;
        u8 forcedLsb = 0;
        const u8 bankRegister = nametableRegisterForAddress(address, forceLsb, forcedLsb);
        mapped = mappedChrAddress(bankRegister, address, forceLsb, forcedLsb, false);
        return true;
    }
    return false;
}

bool Vrc6Mapper::ppuMapWrite(u16 address, u32& mapped) {
    const bool romNametables = (ppuBankingMode_ & 0x10) != 0;
    if (address >= 0x2000 && !romNametables) {
        return false;
    }
    if (address >= 0x2000 && chrBanks_ != 0) {
        mapped = kMapperHandled;
        return true;
    }
    if (chrBanks_ != 0) {
        return false;
    }
    if (address < 0x2000) {
        mapped = mappedChrAddress(patternRegisterForAddress(address), address, false, 0, patternUsesAddressLsb(address));
    } else {
        bool forceLsb = false;
        u8 forcedLsb = 0;
        const u8 bankRegister = nametableRegisterForAddress(address, forceLsb, forcedLsb);
        mapped = mappedChrAddress(bankRegister, address, forceLsb, forcedLsb, false);
    }
    return true;
}

void Vrc6Mapper::clockCpu() {
    for (auto& p : pulse_) {
        if (p.enabled && p.counter-- == 0) {
            p.counter = std::max<u16>(1, p.period);
            p.phase = static_cast<u8>((p.phase + 1) & 0x0f);
        }
    }

    if (saw_.enabled && saw_.counter-- == 0) {
        saw_.counter = std::max<u16>(1, saw_.period);
        saw_.step = static_cast<u8>((saw_.step + 1) % 14);
        if ((saw_.step & 1) == 0) {
            saw_.accumulator = static_cast<u8>(saw_.accumulator + saw_.rate);
        }
        if (saw_.step == 0) {
            saw_.accumulator = 0;
        }
    }

    if (!irqEnabled_) {
        return;
    }
    bool tickIrq = irqCycleMode_;
    if (!irqCycleMode_) {
        irqPrescaler_ -= 3;
        if (irqPrescaler_ <= 0) {
            irqPrescaler_ += 341;
            tickIrq = true;
        }
    }
    if (!tickIrq) {
        return;
    }

    if (irqCounter_ == 0xff) {
        irqCounter_ = irqLatch_;
        irqPending_ = true;
    } else {
        ++irqCounter_;
    }
}

u8 Vrc6Mapper::expansionAudioSample() {
    int mix = 0;
    for (const auto& p : pulse_) {
        if (!p.enabled) {
            continue;
        }
        const bool active = p.constant || p.phase <= p.duty;
        if (active) {
            mix += p.volume;
        }
    }
    if (saw_.enabled) {
        mix += (saw_.accumulator >> 3);
    }
    return static_cast<u8>(std::min(63, mix));
}

void Vrc6Mapper::writePulseRegister(Pulse& pulse, u16 reg, u8 data) {
    switch (reg) {
    case 0:
        pulse.volume = data & 0x0f;
        pulse.duty = static_cast<u8>((data >> 4) & 0x07);
        pulse.constant = (data & 0x80) != 0;
        break;
    case 1:
        pulse.period = static_cast<u16>((pulse.period & 0x0f00) | data);
        break;
    case 2:
        pulse.period = static_cast<u16>(((data & 0x0f) << 8) | (pulse.period & 0x00ff));
        pulse.enabled = (data & 0x80) != 0;
        if (!pulse.enabled) {
            pulse.phase = 0;
        }
        break;
    default:
        break;
    }
}

u8 Vrc6Mapper::patternRegisterForAddress(u16 address) const {
    static constexpr u8 kMode0[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    static constexpr u8 kMode1[8] = {0, 0, 1, 1, 2, 2, 3, 3};
    static constexpr u8 kMode23[8] = {0, 1, 2, 3, 4, 4, 5, 5};

    const u8 page = static_cast<u8>((address >> 10) & 0x07);
    switch (ppuBankingMode_ & 0x03) {
    case 0: return kMode0[page];
    case 1: return kMode1[page];
    default: return kMode23[page];
    }
}

bool Vrc6Mapper::patternUsesAddressLsb(u16 address) const {
    if ((ppuBankingMode_ & 0x20) == 0) {
        return false;
    }

    const u8 mode = ppuBankingMode_ & 0x03;
    const u8 page = static_cast<u8>((address >> 10) & 0x07);
    if (mode == 1) {
        return true;
    }
    if (mode == 2 || mode == 3) {
        return page >= 4;
    }
    return false;
}

u8 Vrc6Mapper::nametableRegisterForAddress(u16 address, bool& forceLsb, u8& forcedLsb) const {
    const u8 page = static_cast<u8>((address >> 10) & 0x03);
    const u8 mode = ppuBankingMode_ & 0x0f;
    forceLsb = (ppuBankingMode_ & 0x20) != 0;

    if (!forceLsb) {
        static constexpr u8 kOther[4][16] = {
            {6, 4, 6, 6, 6, 4, 6, 6, 6, 4, 6, 6, 6, 4, 6, 6},
            {6, 5, 7, 7, 7, 5, 6, 6, 6, 5, 7, 7, 7, 5, 6, 6},
            {7, 6, 6, 6, 6, 6, 7, 7, 7, 6, 6, 6, 6, 6, 7, 7},
            {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7},
        };
        forcedLsb = 0;
        return kOther[page][mode];
    }

    struct NametableBank {
        u8 reg;
        u8 lsb;
    };
    static constexpr NametableBank kMode0[4][4] = {
        {{6, 0}, {6, 1}, {7, 0}, {7, 1}},
        {{6, 0}, {7, 0}, {6, 1}, {7, 1}},
        {{6, 0}, {6, 0}, {7, 0}, {7, 0}},
        {{6, 1}, {7, 1}, {6, 1}, {7, 1}},
    };
    static constexpr NametableBank kMode1[4] = {{4, 0}, {5, 0}, {6, 0}, {7, 0}};
    static constexpr NametableBank kMode2Vertical[4] = {{6, 0}, {7, 0}, {6, 0}, {7, 0}};
    static constexpr NametableBank kMode2Horizontal[4] = {{6, 0}, {6, 0}, {7, 0}, {7, 0}};
    static constexpr NametableBank kMode3[4][4] = {
        {{6, 0}, {7, 0}, {6, 1}, {7, 1}},
        {{6, 0}, {6, 1}, {7, 0}, {7, 1}},
        {{6, 1}, {7, 1}, {6, 1}, {7, 1}},
        {{6, 0}, {6, 0}, {7, 0}, {7, 0}},
    };

    NametableBank bank{};
    switch (mode & 0x03) {
    case 0:
        bank = kMode0[(mode >> 2) & 0x03][page];
        break;
    case 1:
        bank = kMode1[page];
        forceLsb = false;
        break;
    case 2:
        bank = ((mode & 0x04) != 0) ? kMode2Horizontal[page] : kMode2Vertical[page];
        forceLsb = false;
        break;
    case 3:
    default:
        bank = kMode3[(mode >> 2) & 0x03][page];
        break;
    }
    forcedLsb = bank.lsb;
    return bank.reg;
}

u32 Vrc6Mapper::mappedChrAddress(u8 bankRegister, u16 address, bool forceLsb, u8 forcedLsb, bool applyAddressLsb) const {
    const u32 chrCount1k = static_cast<u32>(std::max(1, chrBanks_) * 8);
    u8 bank = chr1k_[bankRegister & 0x07];
    if (forceLsb) {
        bank = static_cast<u8>((bank & 0xfe) | (forcedLsb & 0x01));
    } else if (applyAddressLsb && (ppuBankingMode_ & 0x20)) {
        bank = static_cast<u8>((bank & 0xfe) | ((address >> 10) & 0x01));
    }
    return (static_cast<u32>(bank) % chrCount1k) * 0x0400 + (address & 0x03ff);
}

u16 Vrc6Mapper::decodeRegister(u16 address) const {
    u16 reg = static_cast<u16>(address & 0xf003);
    if (swapAddressLines_) {
        reg = static_cast<u16>((reg & 0xf000) | ((reg & 0x0001) << 1) | ((reg & 0x0002) >> 1));
    }
    return reg;
}

} // namespace nes
