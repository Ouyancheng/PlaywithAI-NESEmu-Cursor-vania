#include "PPU.hpp"

#include <algorithm>

namespace nes {

namespace {

constexpr std::array<Rgb, 64> kPalette = {{
    {0x66,0x66,0x66,255},{0x00,0x2a,0x88,255},{0x14,0x12,0xa7,255},{0x3b,0x00,0xa4,255},{0x5c,0x00,0x7e,255},{0x6e,0x00,0x40,255},{0x6c,0x06,0x00,255},{0x56,0x1d,0x00,255},
    {0x33,0x35,0x00,255},{0x0b,0x48,0x00,255},{0x00,0x52,0x00,255},{0x00,0x4f,0x08,255},{0x00,0x40,0x4d,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},
    {0xad,0xad,0xad,255},{0x15,0x5f,0xd9,255},{0x42,0x40,0xff,255},{0x75,0x27,0xfe,255},{0xa0,0x1a,0xcc,255},{0xb7,0x1e,0x7b,255},{0xb5,0x31,0x20,255},{0x99,0x4e,0x00,255},
    {0x6b,0x6d,0x00,255},{0x38,0x87,0x00,255},{0x0c,0x93,0x00,255},{0x00,0x8f,0x32,255},{0x00,0x7c,0x8d,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},
    {0xff,0xfe,0xff,255},{0x64,0xb0,0xff,255},{0x92,0x90,0xff,255},{0xc6,0x76,0xff,255},{0xf3,0x6a,0xff,255},{0xfe,0x6e,0xcc,255},{0xfe,0x81,0x70,255},{0xea,0x9e,0x22,255},
    {0xbc,0xbe,0x00,255},{0x88,0xd8,0x00,255},{0x5c,0xe4,0x30,255},{0x45,0xe0,0x82,255},{0x48,0xcd,0xde,255},{0x4f,0x4f,0x4f,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},
    {0xff,0xfe,0xff,255},{0xc0,0xdf,0xff,255},{0xd3,0xd2,0xff,255},{0xe8,0xc8,0xff,255},{0xfb,0xc2,0xff,255},{0xfe,0xc4,0xea,255},{0xfe,0xcc,0xc5,255},{0xf7,0xd8,0xa5,255},
    {0xe4,0xe5,0x94,255},{0xcf,0xef,0x96,255},{0xbd,0xf4,0xab,255},{0xb3,0xf3,0xcc,255},{0xb5,0xeb,0xf2,255},{0xb8,0xb8,0xb8,255},{0x00,0x00,0x00,255},{0x00,0x00,0x00,255},
}};

u16 normalizePaletteAddress(u16 address) {
    address &= 0x001f;
    switch (address) {
    case 0x10: return 0x00;
    case 0x14: return 0x04;
    case 0x18: return 0x08;
    case 0x1c: return 0x0c;
    default: return address;
    }
}

} // namespace

void PPU::connect(Cartridge* cartridge) {
    cartridge_ = cartridge;
    scanlineStartEnabled_ = cartridge_ && cartridge_->usesScanlineStart();
    ppuFetchNotificationsEnabled_ = cartridge_ && cartridge_->usesPpuFetchNotifications();
    lastPpuFetchKind_ = PpuFetchKind::Nametable;
}

void PPU::reset() {
    framebuffer_.fill({});
    renderFramebuffer_.fill({});
    nametable_.fill(0);
    palette_.fill(0);
    oam_.fill(0);
    scanlineSprites_.fill(0);
    scanlineSpriteCount_ = 0;
    control_ = mask_ = status_ = oamAddress_ = dataBuffer_ = 0;
    vramAddress_ = tramAddress_ = 0;
    fineX_ = 0;
    bgNextTileId_ = bgNextTileAttr_ = bgNextTileLsb_ = bgNextTileMsb_ = 0;
    bgPatternLo_ = bgPatternHi_ = bgAttribLo_ = bgAttribHi_ = 0;
    writeLatch_ = false;
    scanline_ = 0;
    cycle_ = 0;
    lastPpuFetchKind_ = PpuFetchKind::Nametable;
    oddFrame_ = false;
    nmi_ = false;
    frameComplete_ = false;
}

void PPU::clock() {
    if (scanline_ >= 0 && scanline_ < 240 && cycle_ == 0) {
        prepareScanlineSprites();
    }
    if (scanlineStartEnabled_ && scanline_ >= 0 && scanline_ < 240 && cycle_ == 4 && (mask_ & 0x18)) {
        cartridge_->scanlineStart(scanline_);
    }
    if (scanline_ >= 0 && scanline_ < 240 && cycle_ >= 1 && cycle_ <= 256) {
        drawPixel();
    }
    if (scanline_ == 241 && cycle_ == 1) {
        status_ |= 0x80;
        framebuffer_ = renderFramebuffer_;
        frameComplete_ = true;
        if (control_ & 0x80) {
            nmi_ = true;
        }
    }
    if (scanline_ == 261 && cycle_ == 1) {
        status_ &= static_cast<u8>(~0xe0);
    }
    clockBackgroundFetches();

    ++cycle_;
    if (scanline_ == 261 && cycle_ == 340 && oddFrame_ && renderingEnabled()) {
        cycle_ = 341;
    }
    if (cycle_ >= 341) {
        cycle_ = 0;
        if (cartridge_ && scanline_ >= 0 && scanline_ < 240 && (mask_ & 0x18)) {
            cartridge_->scanline();
        }
        ++scanline_;
        if (scanline_ >= 262) {
            scanline_ = 0;
            oddFrame_ = !oddFrame_;
        }
    }
}

bool PPU::renderingEnabled() const {
    return (mask_ & 0x18) != 0;
}

bool PPU::renderingLine() const {
    return (scanline_ >= 0 && scanline_ < 240) || scanline_ == 261;
}

void PPU::incrementScrollX() {
    if ((vramAddress_ & 0x001f) == 31) {
        vramAddress_ &= static_cast<u16>(~0x001f);
        vramAddress_ ^= 0x0400;
    } else {
        ++vramAddress_;
    }
}

void PPU::incrementScrollY() {
    if ((vramAddress_ & 0x7000) != 0x7000) {
        vramAddress_ = static_cast<u16>(vramAddress_ + 0x1000);
        return;
    }

    vramAddress_ &= static_cast<u16>(~0x7000);
    u16 y = static_cast<u16>((vramAddress_ & 0x03e0) >> 5);
    if (y == 29) {
        y = 0;
        vramAddress_ ^= 0x0800;
    } else if (y == 31) {
        y = 0;
    } else {
        ++y;
    }
    vramAddress_ = static_cast<u16>((vramAddress_ & ~0x03e0) | (y << 5));
}

void PPU::transferScrollX() {
    vramAddress_ = static_cast<u16>((vramAddress_ & 0xfbe0) | (tramAddress_ & 0x041f));
}

void PPU::transferScrollY() {
    vramAddress_ = static_cast<u16>((vramAddress_ & 0x841f) | (tramAddress_ & 0x7be0));
}

void PPU::loadBackgroundShifters() {
    bgPatternLo_ = static_cast<u16>((bgPatternLo_ & 0xff00) | bgNextTileLsb_);
    bgPatternHi_ = static_cast<u16>((bgPatternHi_ & 0xff00) | bgNextTileMsb_);
    bgAttribLo_ = static_cast<u16>((bgAttribLo_ & 0xff00) | ((bgNextTileAttr_ & 0x01) ? 0xff : 0x00));
    bgAttribHi_ = static_cast<u16>((bgAttribHi_ & 0xff00) | ((bgNextTileAttr_ & 0x02) ? 0xff : 0x00));
}

void PPU::shiftBackgroundShifters() {
    if (mask_ & 0x08) {
        bgPatternLo_ <<= 1;
        bgPatternHi_ <<= 1;
        bgAttribLo_ <<= 1;
        bgAttribHi_ <<= 1;
    }
}

void PPU::clockBackgroundFetches() {
    if (!renderingEnabled() || !renderingLine()) {
        return;
    }

    if ((cycle_ >= 1 && cycle_ <= 256) || (cycle_ >= 321 && cycle_ <= 336)) {
        shiftBackgroundShifters();
        switch ((cycle_ - 1) & 0x07) {
        case 0:
            loadBackgroundShifters();
            bgNextTileId_ = ppuRead(static_cast<u16>(0x2000 | (vramAddress_ & 0x0fff)));
            break;
        case 2: {
            const u16 attrAddress = static_cast<u16>(0x23c0 | (vramAddress_ & 0x0c00) |
                                                     ((vramAddress_ >> 4) & 0x38) |
                                                     ((vramAddress_ >> 2) & 0x07));
            u8 attr = ppuRead(attrAddress);
            if (vramAddress_ & 0x0040) {
                attr >>= 4;
            }
            if (vramAddress_ & 0x0002) {
                attr >>= 2;
            }
            bgNextTileAttr_ = static_cast<u8>(attr & 0x03);
            break;
        }
        case 4: {
            notifyMapperPpuFetch(PpuFetchKind::BackgroundPattern);
            const u16 fineY = static_cast<u16>((vramAddress_ >> 12) & 0x0007);
            const u16 table = (control_ & 0x10) ? 0x1000 : 0x0000;
            bgNextTileLsb_ = ppuRead(static_cast<u16>(table + bgNextTileId_ * 16 + fineY));
            break;
        }
        case 6: {
            notifyMapperPpuFetch(PpuFetchKind::BackgroundPattern);
            const u16 fineY = static_cast<u16>((vramAddress_ >> 12) & 0x0007);
            const u16 table = (control_ & 0x10) ? 0x1000 : 0x0000;
            bgNextTileMsb_ = ppuRead(static_cast<u16>(table + bgNextTileId_ * 16 + fineY + 8));
            break;
        }
        case 7:
            incrementScrollX();
            break;
        default:
            break;
        }
    }

    if (cycle_ == 256) {
        incrementScrollY();
    }
    if (cycle_ == 257) {
        loadBackgroundShifters();
        transferScrollX();
    }
    if (scanline_ == 261 && cycle_ >= 280 && cycle_ <= 304) {
        transferScrollY();
    }
    if (cycle_ == 338 || cycle_ == 340) {
        bgNextTileId_ = ppuRead(static_cast<u16>(0x2000 | (vramAddress_ & 0x0fff)));
    }
}

void PPU::notifyMapperPpuFetch(PpuFetchKind kind) {
    if (!ppuFetchNotificationsEnabled_ || lastPpuFetchKind_ == kind) {
        return;
    }
    lastPpuFetchKind_ = kind;
    cartridge_->notifyPpuFetch(kind);
}

void PPU::prepareScanlineSprites() {
    scanlineSpriteCount_ = 0;
    const int spriteHeight = (control_ & 0x20) ? 16 : 8;
    for (int i = 0; i < 64; ++i) {
        const int sy = oam_[i * 4];
        if (scanline_ >= sy + 1 && scanline_ < sy + 1 + spriteHeight) {
            if (scanlineSpriteCount_ < static_cast<int>(scanlineSprites_.size())) {
                scanlineSprites_[scanlineSpriteCount_++] = i;
            } else {
                status_ |= 0x20;
                break;
            }
        }
    }
}

u8 PPU::cpuRead(u16 address) {
    u8 data = 0;
    switch (address & 7) {
    case 2:
        data = static_cast<u8>((status_ & 0xe0) | (dataBuffer_ & 0x1f));
        status_ &= static_cast<u8>(~0x80);
        writeLatch_ = false;
        break;
    case 4:
        data = oam_[oamAddress_];
        break;
    case 7:
        if ((vramAddress_ & 0x3fff) >= 0x3f00) {
            data = ppuRead(vramAddress_);
            dataBuffer_ = ppuRead(static_cast<u16>(vramAddress_ - 0x1000));
        } else {
            data = dataBuffer_;
            dataBuffer_ = ppuRead(vramAddress_);
        }
        vramAddress_ += (control_ & 0x04) ? 32 : 1;
        break;
    default:
        break;
    }
    return data;
}

void PPU::cpuWrite(u16 address, u8 data) {
    switch (address & 7) {
    case 0:
        if ((data & 0x80) && !(control_ & 0x80) && (status_ & 0x80)) {
            nmi_ = true;
        }
        control_ = data;
        tramAddress_ = static_cast<u16>((tramAddress_ & 0xf3ff) | ((data & 0x03) << 10));
        break;
    case 1:
        mask_ = data;
        break;
    case 3:
        oamAddress_ = data;
        break;
    case 4:
        oam_[oamAddress_++] = data;
        break;
    case 5:
        if (!writeLatch_) {
            fineX_ = data & 0x07;
            tramAddress_ = static_cast<u16>((tramAddress_ & 0xffe0) | (data >> 3));
        } else {
            tramAddress_ = static_cast<u16>((tramAddress_ & 0x8fff) | ((data & 0x07) << 12));
            tramAddress_ = static_cast<u16>((tramAddress_ & 0xfc1f) | ((data & 0xf8) << 2));
        }
        writeLatch_ = !writeLatch_;
        break;
    case 6:
        if (!writeLatch_) {
            tramAddress_ = static_cast<u16>((tramAddress_ & 0x00ff) | ((data & 0x3f) << 8));
        } else {
            tramAddress_ = static_cast<u16>((tramAddress_ & 0xff00) | data);
            vramAddress_ = tramAddress_;
        }
        writeLatch_ = !writeLatch_;
        break;
    case 7:
        ppuWrite(vramAddress_, data);
        vramAddress_ += (control_ & 0x04) ? 32 : 1;
        break;
    default:
        break;
    }
}

void PPU::dmaWrite(u8 address, u8 data) {
    oam_[address] = data;
}

bool PPU::pollNmi() {
    const bool out = nmi_;
    nmi_ = false;
    return out;
}

u8 PPU::ppuRead(u16 address) {
    address &= 0x3fff;
    u8 data = 0;
    if (address < 0x3000 && cartridge_ && cartridge_->ppuRead(address, data)) {
        return data;
    }
    if (address < 0x3f00) {
        return nametable_[mirrorNametable(address)];
    }
    return palette_[normalizePaletteAddress(address)] & ((mask_ & 0x01) ? 0x30 : 0x3f);
}

void PPU::ppuWrite(u16 address, u8 data) {
    address &= 0x3fff;
    if (address < 0x3000 && cartridge_ && cartridge_->ppuWrite(address, data)) {
        return;
    }
    if (address < 0x3f00) {
        nametable_[mirrorNametable(address)] = data;
        return;
    }
    palette_[normalizePaletteAddress(address)] = data;
}

u16 PPU::mirrorNametable(u16 address) const {
    const u16 v = static_cast<u16>((address - 0x2000) & 0x0fff);
    const u16 table = v / 0x0400;
    const u16 offset = v & 0x03ff;
    const Mirroring m = cartridge_ ? cartridge_->mirroring() : Mirroring::Horizontal;
    switch (m) {
    case Mirroring::Vertical: return static_cast<u16>((table & 1) * 0x0400 + offset);
    case Mirroring::Horizontal: return static_cast<u16>(((table >> 1) & 1) * 0x0400 + offset);
    case Mirroring::SingleScreenUpper: return static_cast<u16>(0x0400 + offset);
    case Mirroring::FourScreen:
    case Mirroring::SingleScreenLower:
    default: return offset;
    }
}

void PPU::drawPixel() {
    const int x = cycle_ - 1;
    const int y = scanline_;
    if (!(mask_ & 0x18)) {
        renderFramebuffer_[y * kScreenWidth + x] = kPalette[ppuRead(0x3f00) & 0x3f];
        return;
    }

    u8 bgPalette = 0;
    const u8 bgPixel = (mask_ & 0x08) ? backgroundPixel(x, y, bgPalette) : 0;
    Rgb out = colorFromPalette(bgPalette, bgPixel);
    if ((mask_ & 0x10) && (x >= 8 || (mask_ & 0x04))) {
        spritePixel(x, y, bgPixel, out);
    }
    renderFramebuffer_[y * kScreenWidth + x] = out;
}

u8 PPU::backgroundPixel(int x, int y, u8& palette) {
    (void)y;
    if (x < 8 && !(mask_ & 0x02)) {
        palette = 0;
        return 0;
    }

    const u16 mux = static_cast<u16>(0x8000 >> fineX_);
    const u8 p0 = (bgPatternLo_ & mux) ? 1 : 0;
    const u8 p1 = (bgPatternHi_ & mux) ? 1 : 0;
    const u8 a0 = (bgAttribLo_ & mux) ? 1 : 0;
    const u8 a1 = (bgAttribHi_ & mux) ? 1 : 0;
    palette = static_cast<u8>((a1 << 1) | a0);
    return static_cast<u8>((p1 << 1) | p0);
}

u8 PPU::spritePixel(int x, int y, u8 bgPixel, Rgb& color) {
    const int spriteHeight = (control_ & 0x20) ? 16 : 8;
    for (int n = 0; n < scanlineSpriteCount_; ++n) {
        const int i = scanlineSprites_[n];
        const int sy = oam_[i * 4];
        const int sx = oam_[i * 4 + 3];
        if (x < sx || x >= sx + 8 || y < sy + 1 || y >= sy + 1 + spriteHeight) {
            continue;
        }

        const u8 tile = oam_[i * 4 + 1];
        const u8 attr = oam_[i * 4 + 2];
        int row = y - (sy + 1);
        int col = x - sx;
        if (attr & 0x80) row = spriteHeight - 1 - row;
        if (attr & 0x40) col = 7 - col;

        u16 pattern = 0;
        if (spriteHeight == 16) {
            const u16 table = (tile & 1) ? 0x1000 : 0x0000;
            const u8 tileIndex = static_cast<u8>(tile & 0xfe);
            pattern = static_cast<u16>(table + (tileIndex + (row / 8)) * 16 + (row & 7));
        } else {
            pattern = static_cast<u16>(((control_ & 0x08) ? 0x1000 : 0x0000) + tile * 16 + row);
        }

        notifyMapperPpuFetch(PpuFetchKind::SpritePattern);
        const u8 lo = ppuRead(pattern);
        const u8 hi = ppuRead(pattern + 8);
        const u8 bit = static_cast<u8>(7 - col);
        const u8 pixel = static_cast<u8>(((hi >> bit) & 1) << 1 | ((lo >> bit) & 1));
        if (pixel == 0) {
            continue;
        }

        if (i == 0 && bgPixel != 0 && x < 255) {
            status_ |= 0x40;
        }
        if (bgPixel == 0 || !(attr & 0x20)) {
            color = colorFromPalette(static_cast<u8>(4 + (attr & 3)), pixel);
        }
        return pixel;
    }
    return 0;
}

Rgb PPU::colorFromPalette(u8 palette, u8 pixel) {
    const u8 index = pixel == 0 ? ppuRead(0x3f00) : ppuRead(static_cast<u16>(0x3f00 + palette * 4 + pixel));
    return kPalette[index & 0x3f];
}

} // namespace nes
