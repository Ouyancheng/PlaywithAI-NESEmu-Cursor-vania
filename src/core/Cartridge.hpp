#pragma once

#include "Mapper.hpp"
#include "Types.hpp"

#include <filesystem>
#include <memory>

namespace nes {

class Cartridge {
public:
    struct Header {
        int mapper = 0;
        int prgBanks = 0;
        int chrBanks = 0;
        bool hasTrainer = false;
        bool fourScreen = false;
        Mirroring mirroring = Mirroring::Horizontal;
    };

    static std::unique_ptr<Cartridge> loadFromFile(const std::filesystem::path& path, std::string& error);

    Cartridge(Header header, std::vector<u8> prg, std::vector<u8> chr);

    bool cpuRead(u16 address, u8& data);
    bool cpuWrite(u16 address, u8 data);
    bool ppuRead(u16 address, u8& data);
    bool ppuWrite(u16 address, u8 data);
    void reset();
    void clockCpu();
    void scanline();
    bool irqPending() const;
    void clearIrq();
    u8 expansionAudioSample();

    const Header& header() const { return header_; }
    Mirroring mirroring() const;
    std::string mapperName() const;

private:
    Header header_;
    std::vector<u8> prg_;
    std::vector<u8> chr_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace nes
