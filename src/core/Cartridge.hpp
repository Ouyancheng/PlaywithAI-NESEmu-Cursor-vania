#pragma once

#include "Mapper.hpp"
#include "Types.hpp"

#include <filesystem>
#include <memory>

namespace nes {

class Cartridge {
public:
    // Parsed iNES header fields needed to size PRG/CHR storage and choose a mapper.
    struct Header {
        int mapper = 0;
        int prgBanks = 0;
        int chrBanks = 0;
        bool hasTrainer = false;
        bool fourScreen = false;
        Mirroring mirroring = Mirroring::Horizontal;
    };

    // Parse a .nes file, allocate CHR-RAM if needed, and instantiate the mapper.
    static std::unique_ptr<Cartridge> loadFromFile(const std::filesystem::path& path, std::string& error);

    // Construct a cartridge from already-loaded PRG/CHR bytes, primarily used by tests.
    Cartridge(Header header, std::vector<u8> prg, std::vector<u8> chr);

    // CPU read/write entry points delegated to the active mapper.
    bool cpuRead(u16 address, u8& data);
    bool cpuWrite(u16 address, u8 data);
    // PPU read/write entry points delegated to the active mapper or CHR storage.
    bool ppuRead(u16 address, u8& data);
    bool ppuWrite(u16 address, u8 data);
    // Forward reset and clock hooks to the mapper.
    void reset();
    void clockCpu();
    void scanline();
    // Optional early scanline and fetch-context hooks used by MMC5.
    bool usesScanlineStart() const;
    void scanlineStart(int scanline);
    bool usesPpuFetchNotifications() const;
    void notifyPpuFetch(PpuFetchKind kind);
    // Forward mapper IRQ and expansion-audio state.
    bool irqPending() const;
    void clearIrq();
    u8 expansionAudioSample();

    const Header& header() const { return header_; }
    // Current mapper-controlled mirroring mode.
    Mirroring mirroring() const;
    // Human-readable mapper name for UI and diagnostics.
    std::string mapperName() const;

private:
    Header header_;
    std::vector<u8> prg_;
    std::vector<u8> chr_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace nes
