#pragma once

#include "Cartridge.hpp"
#include "Types.hpp"

#include <array>

namespace nes {

class PPU {
public:
    // Attach cartridge-backed CHR/nametable access and cache mapper hook capabilities.
    void connect(Cartridge* cartridge);
    // Reset PPU registers, scroll state, OAM, palettes, and frame buffers.
    void reset();
    // Advance one PPU dot, including fetches, pixel output, vblank, and scanline rollover.
    void clock();

    // CPU-facing PPU register access for $2000-$2007.
    u8 cpuRead(u16 address);
    void cpuWrite(u16 address, u8 data);
    // OAM byte write used by CPU $4014 DMA.
    void dmaWrite(u8 address, u8 data);
    // Return and clear the pending NMI line.
    bool pollNmi();
    // True after a completed frame has been published at vblank.
    bool frameComplete() const { return frameComplete_; }
    void clearFrameComplete() { frameComplete_ = false; }
    // Latest completed frame. The renderer should only read this when synchronized.
    const Framebuffer& framebuffer() const { return framebuffer_; }

private:
    // Internal PPU memory read/write after address mirroring and cartridge delegation.
    u8 ppuRead(u16 address);
    void ppuWrite(u16 address, u8 data);
    // Convert $2000-$2FFF nametable addresses through the active mirroring mode.
    u16 mirrorNametable(u16 address) const;
    bool renderingEnabled() const;
    bool renderingLine() const;
    // Loopy-scroll increments/transfers performed at hardware-compatible dots.
    void incrementScrollX();
    void incrementScrollY();
    void transferScrollX();
    void transferScrollY();
    // Background shifter pipeline: load, shift, fetch next tile data.
    void loadBackgroundShifters();
    void shiftBackgroundShifters();
    void clockBackgroundFetches();
    // Select up to eight sprites that can affect the current scanline.
    void prepareScanlineSprites();
    // Compose one visible pixel from background and sprite sources.
    void drawPixel();
    // Notify mappers such as MMC5 only when fetch context changes.
    void notifyMapperPpuFetch(PpuFetchKind kind);
    u8 backgroundPixel(int x, int y, u8& palette);
    u8 spritePixel(int x, int y, u8 bgPixel, Rgb& color);
    Rgb colorFromPalette(u8 palette, u8 pixel);

    Cartridge* cartridge_ = nullptr;
    bool scanlineStartEnabled_ = false;
    bool ppuFetchNotificationsEnabled_ = false;
    PpuFetchKind lastPpuFetchKind_ = PpuFetchKind::Nametable;
    Framebuffer framebuffer_{};
    Framebuffer renderFramebuffer_{};
    std::array<u8, 2048> nametable_{};
    std::array<u8, 32> palette_{};
    std::array<u8, 256> oam_{};
    std::array<int, 8> scanlineSprites_{};
    int scanlineSpriteCount_ = 0;

    u8 control_ = 0;
    u8 mask_ = 0;
    u8 status_ = 0;
    u8 oamAddress_ = 0;
    u8 dataBuffer_ = 0;
    u16 vramAddress_ = 0;
    u16 tramAddress_ = 0;
    u8 fineX_ = 0;
    u8 bgNextTileId_ = 0;
    u8 bgNextTileAttr_ = 0;
    u8 bgNextTileLsb_ = 0;
    u8 bgNextTileMsb_ = 0;
    u16 bgPatternLo_ = 0;
    u16 bgPatternHi_ = 0;
    u16 bgAttribLo_ = 0;
    u16 bgAttribHi_ = 0;
    bool writeLatch_ = false;
    int scanline_ = 0;
    int cycle_ = 0;
    bool oddFrame_ = false;
    bool nmi_ = false;
    bool frameComplete_ = false;
};

} // namespace nes
