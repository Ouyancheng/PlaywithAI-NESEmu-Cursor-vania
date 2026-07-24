#pragma once

#include "Types.hpp"

namespace nes {

constexpr u32 kMapperHandled = 0xffffffff;

enum class Mirroring {
    Horizontal,
    Vertical,
    SingleScreenLower,
    SingleScreenUpper,
    FourScreen,
};

enum class PpuFetchKind {
    Nametable,
    Attribute,
    BackgroundPattern,
    SpritePattern,
};

class Mapper {
public:
    virtual ~Mapper() = default;

    virtual bool cpuMapRead(u16 address, u32& mapped, u8& data) = 0;
    virtual bool cpuMapWrite(u16 address, u32& mapped, u8 data) = 0;
    virtual bool ppuMapRead(u16 address, u32& mapped, u8& data) = 0;
    virtual bool ppuMapWrite(u16 address, u32& mapped, u8 data) = 0;
    virtual void reset() {}
    virtual void clockCpu() {}
    virtual void scanline() {}
    virtual bool usesScanlineStart() const { return false; }
    virtual void scanlineStart(int) {}
    virtual bool usesPpuFetchNotifications() const { return false; }
    virtual void notifyPpuFetch(PpuFetchKind) {}
    virtual bool irqPending() const { return false; }
    virtual void clearIrq() {}
    virtual u8 expansionAudioSample() { return 0; }
    virtual Mirroring mirroring() const = 0;
};

} // namespace nes
