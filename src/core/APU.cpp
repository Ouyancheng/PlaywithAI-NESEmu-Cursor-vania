#include "APU.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nes {

namespace {

constexpr u8 kLengthTable[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

constexpr u16 kDmcPeriodTable[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 84, 72, 54,
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
    frameFiveStep_ = false;
    frameIrqInhibit_ = false;
    frameIrqPending_ = false;
    dmcIrqEnabled_ = false;
    dmcLoop_ = false;
    dmcIrqPending_ = false;
    dmcEnabled_ = false;
    dmcTimer_ = kDmcPeriodTable[0];
    dmcCounter_ = dmcTimer_;
    dmcOutputLevel_ = 0;
    dmcSampleAddress_ = 0xc000;
    dmcSampleLength_ = 1;
    dmcCurrentAddress_ = dmcSampleAddress_;
    dmcBytesRemaining_ = 0;
    dmcSampleBuffer_ = 0;
    dmcSampleBufferEmpty_ = true;
    dmcShiftRegister_ = 0;
    dmcBitsRemaining_ = 8;
    dmcSilence_ = true;
    clock_ = 0;
    sampleClock_ = 0.0;
    dcBlockPrevInput_ = 0.0f;
    dcBlockPrevOutput_ = 0.0f;
    lowPassOutput_ = 0.0f;
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
    clockDmc();

    if (frameFiveStep_) {
        if (clock_ == 7457 || clock_ == 14913 || clock_ == 22371 || clock_ >= 37281) {
            quarterFrame();
        }
        if (clock_ == 14913 || clock_ >= 37281) {
            halfFrame();
        }
        if (clock_ >= 37281) {
            clock_ = 0;
        }
    } else {
        if (clock_ == 7457 || clock_ == 14913 || clock_ == 22371 || clock_ >= 29829) {
            quarterFrame();
        }
        if (clock_ == 14913 || clock_ >= 29829) {
            halfFrame();
        }
        if (clock_ >= 29829) {
            if (!frameIrqInhibit_) {
                frameIrqPending_ = true;
            }
            clock_ = 0;
        }
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
    } else if (address == 0x4010) {
        dmcIrqEnabled_ = (data & 0x80) != 0;
        dmcLoop_ = (data & 0x40) != 0;
        dmcTimer_ = kDmcPeriodTable[data & 0x0f];
        if (!dmcIrqEnabled_) {
            dmcIrqPending_ = false;
        }
    } else if (address == 0x4011) {
        dmcOutputLevel_ = data & 0x7f;
    } else if (address == 0x4012) {
        dmcSampleAddress_ = static_cast<u16>(0xc000 | (data << 6));
    } else if (address == 0x4013) {
        dmcSampleLength_ = static_cast<u16>((data << 4) | 0x0001);
    } else if (address == 0x4015) {
        pulse_[0].enabled = (data & 0x01) != 0;
        pulse_[1].enabled = (data & 0x02) != 0;
        triangleEnabled_ = (data & 0x04) != 0;
        noiseEnabled_ = (data & 0x08) != 0;
        dmcEnabled_ = (data & 0x10) != 0;
        if (!pulse_[0].enabled) pulse_[0].lengthCounter = 0;
        if (!pulse_[1].enabled) pulse_[1].lengthCounter = 0;
        if (!triangleEnabled_) triangleLengthCounter_ = 0;
        if (!noiseEnabled_) noiseLengthCounter_ = 0;
        if (!dmcEnabled_) {
            dmcBytesRemaining_ = 0;
        } else if (dmcBytesRemaining_ == 0) {
            restartDmcSample();
        }
        dmcIrqPending_ = false;
    } else if (address == 0x4017) {
        frameFiveStep_ = (data & 0x80) != 0;
        frameIrqInhibit_ = (data & 0x40) != 0;
        if (frameIrqInhibit_) {
            frameIrqPending_ = false;
        }
        clock_ = 0;
        if (frameFiveStep_) {
            quarterFrame();
            halfFrame();
        }
    }
}

u8 APU::cpuRead(u16) {
    const u8 status = static_cast<u8>((pulse_[0].lengthCounter ? 0x01 : 0) |
                                      (pulse_[1].lengthCounter ? 0x02 : 0) |
                                      (triangleLengthCounter_ ? 0x04 : 0) |
                                      (noiseLengthCounter_ ? 0x08 : 0) |
                                      (dmcBytesRemaining_ ? 0x10 : 0) |
                                      (frameIrqPending_ ? 0x40 : 0) |
                                      (dmcIrqPending_ ? 0x80 : 0));
    frameIrqPending_ = false;
    return status;
}

void APU::setDmcReader(std::function<u8(u16)> reader) {
    dmcReader_ = std::move(reader);
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
    const float dmcOut = static_cast<float>(dmcOutputLevel_);
    const float tndDenominator = triangleOut / 8227.0f + noiseOut / 12241.0f + dmcOut / 22638.0f;
    const float tndOut = tndDenominator > 0.0f ? 159.79f / ((1.0f / tndDenominator) + 100.0f) : 0.0f;
    const float expansionOut = static_cast<float>(expansionLevel_) / 128.0f;
    const float mixed = (pulseOut + tndOut + expansionOut) * 1.18f;

    constexpr float sampleRate = 44100.0f;
    constexpr float pi = 3.14159265358979323846f;
    const float dc = std::exp(-2.0f * pi * 20.0f / sampleRate);
    const float lp18k = 1.0f - std::exp(-2.0f * pi * 18000.0f / sampleRate);

    const float dcBlocked = dc * (dcBlockPrevOutput_ + mixed - dcBlockPrevInput_);
    dcBlockPrevInput_ = mixed;
    dcBlockPrevOutput_ = dcBlocked;

    lowPassOutput_ += lp18k * (dcBlocked - lowPassOutput_);
    return std::clamp(lowPassOutput_, -1.0f, 1.0f);
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

void APU::clockDmc() {
    if (dmcSampleBufferEmpty_ && dmcBytesRemaining_ > 0 && dmcReader_) {
        dmcSampleBuffer_ = dmcReader_(dmcCurrentAddress_);
        dmcSampleBufferEmpty_ = false;
        dmcCurrentAddress_ = dmcCurrentAddress_ == 0xffff ? 0x8000 : static_cast<u16>(dmcCurrentAddress_ + 1);
        --dmcBytesRemaining_;
        if (dmcBytesRemaining_ == 0) {
            if (dmcLoop_) {
                restartDmcSample();
            } else if (dmcIrqEnabled_) {
                dmcIrqPending_ = true;
            }
        }
    }

    if (dmcCounter_ > 0) {
        --dmcCounter_;
        return;
    }
    dmcCounter_ = dmcTimer_;

    if (dmcBitsRemaining_ == 0) {
        dmcBitsRemaining_ = 8;
        if (dmcSampleBufferEmpty_) {
            dmcSilence_ = true;
        } else {
            dmcSilence_ = false;
            dmcShiftRegister_ = dmcSampleBuffer_;
            dmcSampleBufferEmpty_ = true;
        }
    }

    if (!dmcSilence_) {
        if (dmcShiftRegister_ & 0x01) {
            if (dmcOutputLevel_ <= 125) {
                dmcOutputLevel_ = static_cast<u8>(dmcOutputLevel_ + 2);
            }
        } else if (dmcOutputLevel_ >= 2) {
            dmcOutputLevel_ = static_cast<u8>(dmcOutputLevel_ - 2);
        }
    }

    dmcShiftRegister_ >>= 1;
    --dmcBitsRemaining_;
}

void APU::restartDmcSample() {
    dmcCurrentAddress_ = dmcSampleAddress_;
    dmcBytesRemaining_ = dmcSampleLength_;
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
