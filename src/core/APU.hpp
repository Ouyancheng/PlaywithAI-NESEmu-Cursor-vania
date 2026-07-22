#pragma once

#include "Types.hpp"

#include <functional>
#include <vector>

namespace nes {

class APU {
public:
    void reset();
    void clock();
    void cpuWrite(u16 address, u8 data);
    u8 cpuRead(u16 address);
    void setDmcReader(std::function<u8(u16)> reader);
    void setExpansionAudio(u8 level);
    std::vector<float> takeSamples();

private:
    struct Pulse {
        u8 duty = 0;
        u8 volume = 0;
        u8 envelopeVolume = 0;
        u8 envelopeDivider = 0;
        bool envelopeStart = false;
        bool constantVolume = false;
        bool loop = false;
        u16 timer = 1;
        u16 counter = 0;
        u8 lengthCounter = 0;
        u8 seq = 0;
        bool enabled = false;
        bool sweepEnabled = false;
        bool sweepNegate = false;
        bool sweepReload = false;
        u8 sweepPeriod = 0;
        u8 sweepDivider = 0;
        u8 sweepShift = 0;
    };

    void quarterFrame();
    void halfFrame();
    u16 pulseSweepTarget(const Pulse& pulse, int channel) const;
    void clockPulseSweep(Pulse& pulse, int channel);
    void clockDmc();
    void restartDmcSample();
    float nextSample();

    Pulse pulse_[2];
    u16 triangleTimer_ = 1;
    u16 triangleCounter_ = 0;
    u8 triangleLengthCounter_ = 0;
    u8 triangleLinearCounter_ = 0;
    u8 triangleLinearReload_ = 0;
    bool triangleControl_ = false;
    bool triangleReload_ = false;
    u8 triangleStep_ = 0;
    bool triangleEnabled_ = false;
    u16 noiseLfsr_ = 1;
    u16 noiseTimer_ = 1;
    u16 noiseCounter_ = 0;
    u8 noiseVolume_ = 0;
    u8 noiseEnvelopeVolume_ = 0;
    u8 noiseEnvelopeDivider_ = 0;
    u8 noiseLengthCounter_ = 0;
    bool noiseEnvelopeStart_ = false;
    bool noiseConstantVolume_ = false;
    bool noiseLoop_ = false;
    bool noiseMode_ = false;
    bool noiseEnabled_ = false;
    bool frameFiveStep_ = false;
    bool frameIrqInhibit_ = false;
    bool frameIrqPending_ = false;
    bool dmcIrqEnabled_ = false;
    bool dmcLoop_ = false;
    bool dmcIrqPending_ = false;
    bool dmcEnabled_ = false;
    u16 dmcTimer_ = 428;
    u16 dmcCounter_ = 428;
    u8 dmcOutputLevel_ = 0;
    u16 dmcSampleAddress_ = 0xc000;
    u16 dmcSampleLength_ = 1;
    u16 dmcCurrentAddress_ = 0xc000;
    u16 dmcBytesRemaining_ = 0;
    u8 dmcSampleBuffer_ = 0;
    bool dmcSampleBufferEmpty_ = true;
    u8 dmcShiftRegister_ = 0;
    u8 dmcBitsRemaining_ = 8;
    bool dmcSilence_ = true;
    std::function<u8(u16)> dmcReader_;
    u64 clock_ = 0;
    double sampleClock_ = 0.0;
    float dcBlockPrevInput_ = 0.0f;
    float dcBlockPrevOutput_ = 0.0f;
    float lowPassOutput_ = 0.0f;
    u8 expansionLevel_ = 0;
    std::vector<float> samples_;
};

} // namespace nes
