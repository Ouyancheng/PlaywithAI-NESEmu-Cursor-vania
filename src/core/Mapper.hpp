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

    // Map a CPU read. Set mapped to a PRG offset, or kMapperHandled if data was produced internally.
    virtual bool cpuMapRead(u16 address, u32& mapped, u8& data) = 0;
    // Map a CPU write. Return true only when the cartridge should stop bus-side handling.
    virtual bool cpuMapWrite(u16 address, u32& mapped, u8 data) = 0;
    // Map a PPU read. Mappers with ExRAM/fill data may return kMapperHandled and fill data directly.
    virtual bool ppuMapRead(u16 address, u32& mapped, u8& data) = 0;
    // Map a PPU write. CHR-RAM writes return a CHR offset; mapper-owned nametables use kMapperHandled.
    virtual bool ppuMapWrite(u16 address, u32& mapped, u8 data) = 0;
    // Reset mapper registers to power-on state while preserving battery-like RAM where appropriate.
    virtual void reset() {}
    // Clock mapper logic that advances at CPU rate, such as expansion audio or CPU-cycle IRQs.
    virtual void clockCpu() {}
    // End-of-visible-scanline hook used by MMC3-style IRQ counters.
    virtual void scanline() {}
    // Opt-in for mappers that need an early scanline hook, such as MMC5.
    virtual bool usesScanlineStart() const { return false; }
    virtual void scanlineStart(int) {}
    // Opt-in for expensive PPU fetch notifications. Most mappers keep this disabled.
    virtual bool usesPpuFetchNotifications() const { return false; }
    virtual void notifyPpuFetch(PpuFetchKind) {}
    // Mapper IRQ line state, sampled by Nes before CPU instruction boundaries.
    virtual bool irqPending() const { return false; }
    virtual void clearIrq() {}
    // Optional expansion audio level mixed by the APU once per CPU cycle.
    virtual u8 expansionAudioSample() { return 0; }
    // Current nametable mirroring mode exposed to the PPU.
    virtual Mirroring mirroring() const = 0;
};

} // namespace nes
