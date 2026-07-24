#pragma once

#include "Cartridge.hpp"
#include "Types.hpp"

#include <array>

namespace nes {

class PPU {
public:
    void connect(Cartridge* cartridge);
    void reset();
    void clock();

    u8 cpuRead(u16 address);
    void cpuWrite(u16 address, u8 data);
    void dmaWrite(u8 address, u8 data);
    bool pollNmi();
    bool frameComplete() const { return frameComplete_; }
    void clearFrameComplete() { frameComplete_ = false; }
    const Framebuffer& framebuffer() const { return framebuffer_; }

private:
    u8 ppuRead(u16 address);
    void ppuWrite(u16 address, u8 data);
    u16 mirrorNametable(u16 address) const;
    bool renderingEnabled() const;
    bool renderingLine() const;
    void incrementScrollX();
    void incrementScrollY();
    void transferScrollX();
    void transferScrollY();
    void loadBackgroundShifters();
    void shiftBackgroundShifters();
    void clockBackgroundFetches();
    void prepareScanlineSprites();
    void drawPixel();
    u8 backgroundPixel(int x, int y, u8& palette);
    u8 spritePixel(int x, int y, u8 bgPixel, Rgb& color);
    Rgb colorFromPalette(u8 palette, u8 pixel);

    Cartridge* cartridge_ = nullptr;
    bool ppuFetchNotificationsEnabled_ = false;
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
