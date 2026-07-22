#include "APU.hpp"

#include <algorithm>
#include <cmath>

namespace nes {

namespace {

constexpr u8 kLengthTable[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

}

void APU::reset() {
    pulse_[0] = {};
    pulse_[1] = {};
    triangleTimer_ = 1;
    triangleCounter_ = 0;
    triangleLengthCounter_ = 0;
    triangleLinearCounter_ = 0;
    triangleLinearReload_ = 0;
    triangleControl_ = false;
    triangleReload_ = false;
    triangleStep_ = 0;
    triangleEnabled_ = false;
    noiseLfsr_ = 1;
    noiseTimer_ = 1;
    noiseCounter_ = 0;
    noiseVolume_ = 0;
    noiseEnvelopeVolume_ = 0;
    noiseEnvelopeDivider_ = 0;
    noiseLengthCounter_ = 0;
    noiseEnvelopeStart_ = false;
    noiseConstantVolume_ = false;
    noiseLoop_ = false;
    noiseMode_ = false;
    noiseEnabled_ = false;
    clock_ = 0;
    sampleClock_ = 0.0;
    filteredSample_ = 0.0f;
    expansionLevel_ = 0;
    samples_.clear();
}

void APU::clock() {
    ++clock_;
    for (auto& p : pulse_) {
        if (p.enabled && p.lengthCounter > 0 && p.counter-- == 0) {
            p.counter = static_cast<u16>((std::max<u16>(1, p.timer) + 1) * 2);
            p.seq = static_cast<u8>((p.seq + 1) & 7);
        }
    }
    if (triangleEnabled_ && triangleLengthCounter_ > 0 && triangleLinearCounter_ > 0 && triangleCounter_-- == 0) {
        triangleCounter_ = static_cast<u16>(std::max<u16>(1, triangleTimer_) + 1);
        triangleStep_ = static_cast<u8>((triangleStep_ + 1) & 31);
    }
    if (noiseEnabled_ && noiseLengthCounter_ > 0 && noiseCounter_-- == 0) {
        noiseCounter_ = std::max<u16>(1, noiseTimer_);
        const u16 tap = noiseMode_ ? 6 : 1;
        const u16 feedback = static_cast<u16>((noiseLfsr_ ^ (noiseLfsr_ >> tap)) & 1);
        noiseLfsr_ = static_cast<u16>((noiseLfsr_ >> 1) | (feedback << 14));
    }

    if (clock_ == 7457 || clock_ == 14913 || clock_ == 22371 || clock_ >= 29829) {
        quarterFrame();
    }
    if (clock_ == 14913 || clock_ >= 29829) {
        halfFrame();
    }
    if (clock_ >= 29829) {
        clock_ = 0;
    }

    sampleClock_ += 44100.0 / static_cast<double>(kCpuFrequencyNtsc);
    if (sampleClock_ >= 1.0) {
        sampleClock_ -= 1.0;
        samples_.push_back(nextSample());
    }
}

void APU::cpuWrite(u16 address, u8 data) {
    auto writePulse = [&](Pulse& p, u16 base) {
        switch (address - base) {
        case 0:
            p.duty = static_cast<u8>((data >> 6) & 3);
            p.volume = data & 0x0f;
            p.constantVolume = (data & 0x10) != 0;
            p.loop = (data & 0x20) != 0;
            break;
        case 1:
            p.sweepEnabled = (data & 0x80) != 0;
            p.sweepPeriod = static_cast<u8>((data >> 4) & 0x07);
            p.sweepNegate = (data & 0x08) != 0;
            p.sweepShift = data & 0x07;
            p.sweepReload = true;
            break;
        case 2:
            p.timer = static_cast<u16>((p.timer & 0x0700) | data);
            break;
        case 3:
            p.timer = static_cast<u16>(((data & 0x07) << 8) | (p.timer & 0x00ff));
            p.lengthCounter = kLengthTable[data >> 3];
            p.envelopeStart = true;
            p.seq = 0;
            break;
        default:
            break;
        }
    };

    if (address >= 0x4000 && address <= 0x4003) {
        writePulse(pulse_[0], 0x4000);
    } else if (address >= 0x4004 && address <= 0x4007) {
        writePulse(pulse_[1], 0x4004);
    } else if (address == 0x4008) {
        triangleControl_ = (data & 0x80) != 0;
        triangleLinearReload_ = data & 0x7f;
    } else if (address == 0x400a) {
        triangleTimer_ = static_cast<u16>((triangleTimer_ & 0x0700) | data);
    } else if (address == 0x400b) {
        triangleTimer_ = static_cast<u16>(((data & 0x07) << 8) | (triangleTimer_ & 0x00ff));
        triangleLengthCounter_ = kLengthTable[data >> 3];
        triangleReload_ = true;
    } else if (address == 0x400c) {
        noiseVolume_ = data & 0x0f;
        noiseConstantVolume_ = (data & 0x10) != 0;
        noiseLoop_ = (data & 0x20) != 0;
    } else if (address == 0x400e) {
        static constexpr u16 periods[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
        noiseTimer_ = periods[data & 0x0f];
        noiseMode_ = (data & 0x80) != 0;
    } else if (address == 0x400f) {
        noiseLengthCounter_ = kLengthTable[data >> 3];
        noiseEnvelopeStart_ = true;
    } else if (address == 0x4015) {
        pulse_[0].enabled = (data & 0x01) != 0;
        pulse_[1].enabled = (data & 0x02) != 0;
        triangleEnabled_ = (data & 0x04) != 0;
        noiseEnabled_ = (data & 0x08) != 0;
        if (!pulse_[0].enabled) pulse_[0].lengthCounter = 0;
        if (!pulse_[1].enabled) pulse_[1].lengthCounter = 0;
        if (!triangleEnabled_) triangleLengthCounter_ = 0;
        if (!noiseEnabled_) noiseLengthCounter_ = 0;
    }
}

u8 APU::cpuRead(u16) {
    return static_cast<u8>((pulse_[0].lengthCounter ? 0x01 : 0) |
                           (pulse_[1].lengthCounter ? 0x02 : 0) |
                           (triangleLengthCounter_ ? 0x04 : 0) |
                           (noiseLengthCounter_ ? 0x08 : 0));
}

void APU::setExpansionAudio(u8 level) {
    expansionLevel_ = level;
}

std::vector<float> APU::takeSamples() {
    std::vector<float> out;
    out.swap(samples_);
    return out;
}

float APU::nextSample() {
    static constexpr u8 dutyTable[4][8] = {
        {0,1,0,0,0,0,0,0},
        {0,1,1,0,0,0,0,0},
        {0,1,1,1,1,0,0,0},
        {1,0,0,1,1,1,1,1},
    };
    float pulseSum = 0.0f;
    for (int i = 0; i < 2; ++i) {
        const auto& p = pulse_[i];
        if (p.enabled && p.lengthCounter > 0 && p.timer >= 8 && pulseSweepTarget(p, i) <= 0x07ff && dutyTable[p.duty][p.seq]) {
            const u8 vol = p.constantVolume ? p.volume : p.envelopeVolume;
            pulseSum += static_cast<float>(vol);
        }
    }
    const float pulseOut = pulseSum > 0.0f ? 95.88f / ((8128.0f / pulseSum) + 100.0f) : 0.0f;

    float triangleOut = 0.0f;
    if (triangleEnabled_ && triangleLengthCounter_ > 0 && triangleLinearCounter_ > 0) {
        const int tri = triangleStep_ < 16 ? triangleStep_ : 31 - triangleStep_;
        triangleOut = static_cast<float>(tri);
    }
    float noiseOut = 0.0f;
    if (noiseEnabled_ && noiseLengthCounter_ > 0 && (noiseLfsr_ & 1) == 0) {
        noiseOut = static_cast<float>(noiseConstantVolume_ ? noiseVolume_ : noiseEnvelopeVolume_);
    }
    const float tndDenominator = triangleOut / 8227.0f + noiseOut / 12241.0f;
    const float tndOut = tndDenominator > 0.0f ? 159.79f / ((1.0f / tndDenominator) + 100.0f) : 0.0f;
    const float expansionOut = static_cast<float>(expansionLevel_) / 96.0f;
    const float mixed = std::clamp((pulseOut + tndOut + expansionOut) * 1.75f, -1.0f, 1.0f);
    filteredSample_ += 0.18f * (mixed - filteredSample_);
    return filteredSample_;
}

void APU::quarterFrame() {
    for (auto& p : pulse_) {
        if (p.envelopeStart) {
            p.envelopeStart = false;
            p.envelopeVolume = 15;
            p.envelopeDivider = p.volume;
        } else if (p.envelopeDivider == 0) {
            p.envelopeDivider = p.volume;
            if (p.envelopeVolume > 0) {
                --p.envelopeVolume;
            } else if (p.loop) {
                p.envelopeVolume = 15;
            }
        } else {
            --p.envelopeDivider;
        }
    }

    if (noiseEnvelopeStart_) {
        noiseEnvelopeStart_ = false;
        noiseEnvelopeVolume_ = 15;
        noiseEnvelopeDivider_ = noiseVolume_;
    } else if (noiseEnvelopeDivider_ == 0) {
        noiseEnvelopeDivider_ = noiseVolume_;
        if (noiseEnvelopeVolume_ > 0) {
            --noiseEnvelopeVolume_;
        } else if (noiseLoop_) {
            noiseEnvelopeVolume_ = 15;
        }
    } else {
        --noiseEnvelopeDivider_;
    }

    if (triangleReload_) {
        triangleLinearCounter_ = triangleLinearReload_;
    } else if (triangleLinearCounter_ > 0) {
        --triangleLinearCounter_;
    }
    if (!triangleControl_) {
        triangleReload_ = false;
    }
}

void APU::halfFrame() {
    for (auto& p : pulse_) {
        if (!p.loop && p.lengthCounter > 0) {
            --p.lengthCounter;
        }
    }
    clockPulseSweep(pulse_[0], 0);
    clockPulseSweep(pulse_[1], 1);
    if (!triangleControl_ && triangleLengthCounter_ > 0) {
        --triangleLengthCounter_;
    }
    if (!noiseLoop_ && noiseLengthCounter_ > 0) {
        --noiseLengthCounter_;
    }
}

u16 APU::pulseSweepTarget(const Pulse& pulse, int channel) const {
    const u16 change = static_cast<u16>(pulse.timer >> pulse.sweepShift);
    if (!pulse.sweepNegate) {
        return static_cast<u16>(pulse.timer + change);
    }
    const u16 adjust = static_cast<u16>(change + (channel == 0 ? 1 : 0));
    return pulse.timer > adjust ? static_cast<u16>(pulse.timer - adjust) : 0;
}

void APU::clockPulseSweep(Pulse& pulse, int channel) {
    const bool dividerExpired = pulse.sweepDivider == 0;
    if (dividerExpired && pulse.sweepEnabled && pulse.sweepShift > 0 && pulse.timer >= 8) {
        const u16 target = pulseSweepTarget(pulse, channel);
        if (target <= 0x07ff) {
            pulse.timer = target;
        }
    }

    if (dividerExpired || pulse.sweepReload) {
        pulse.sweepDivider = pulse.sweepPeriod;
        pulse.sweepReload = false;
    } else {
        --pulse.sweepDivider;
    }
}

} // namespace nes
