#include "Cartridge.hpp"

#include "mappers/Mmc1Mapper.hpp"
#include "mappers/Mmc3Mapper.hpp"
#include "mappers/Mmc5Mapper.hpp"
#include "mappers/NromMapper.hpp"
#include "mappers/UxromMapper.hpp"
#include "mappers/Vrc6Mapper.hpp"

#include <fstream>
#include <iterator>

namespace nes {

namespace {

std::unique_ptr<Mapper> makeMapper(const Cartridge::Header& header) {
    switch (header.mapper) {
    case 0:
        return std::make_unique<NromMapper>(header.prgBanks, header.chrBanks, header.mirroring);
    case 1:
        return std::make_unique<Mmc1Mapper>(header.prgBanks, header.chrBanks, header.mirroring);
    case 2:
        return std::make_unique<UxromMapper>(header.prgBanks, header.chrBanks, header.mirroring);
    case 4:
        return std::make_unique<Mmc3Mapper>(header.prgBanks, header.chrBanks, header.mirroring);
    case 5:
        return std::make_unique<Mmc5Mapper>(header.prgBanks, header.chrBanks, header.mirroring);
    case 24:
        return std::make_unique<Vrc6Mapper>(header.prgBanks, header.chrBanks, header.mirroring, false);
    case 26:
        return std::make_unique<Vrc6Mapper>(header.prgBanks, header.chrBanks, header.mirroring, true);
    default:
        return nullptr;
    }
}

} // namespace

std::unique_ptr<Cartridge> Cartridge::loadFromFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open ROM file.";
        return nullptr;
    }

    std::vector<u8> bytes(std::istreambuf_iterator<char>(file), {});
    if (bytes.size() < 16 || bytes[0] != 'N' || bytes[1] != 'E' || bytes[2] != 'S' || bytes[3] != 0x1a) {
        error = "Invalid iNES ROM header.";
        return nullptr;
    }

    Header header;
    header.prgBanks = bytes[4];
    header.chrBanks = bytes[5];
    header.hasTrainer = (bytes[6] & 0x04) != 0;
    header.fourScreen = (bytes[6] & 0x08) != 0;
    header.mapper = (bytes[6] >> 4) | (bytes[7] & 0xf0);
    header.mirroring = header.fourScreen ? Mirroring::FourScreen : ((bytes[6] & 1) ? Mirroring::Vertical : Mirroring::Horizontal);

    const std::size_t trainer = header.hasTrainer ? 512 : 0;
    const std::size_t prgSize = static_cast<std::size_t>(header.prgBanks) * 16 * 1024;
    const std::size_t chrSize = static_cast<std::size_t>(header.chrBanks) * 8 * 1024;
    const std::size_t prgOffset = 16 + trainer;
    const std::size_t chrOffset = prgOffset + prgSize;
    if (bytes.size() < chrOffset + chrSize || header.prgBanks == 0) {
        error = "ROM file is truncated or missing PRG data.";
        return nullptr;
    }

    std::vector<u8> prg(bytes.begin() + static_cast<std::ptrdiff_t>(prgOffset), bytes.begin() + static_cast<std::ptrdiff_t>(chrOffset));
    std::vector<u8> chr;
    if (chrSize != 0) {
        chr.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chrOffset), bytes.begin() + static_cast<std::ptrdiff_t>(chrOffset + chrSize));
    } else {
        chr.resize(8 * 1024);
    }

    auto cart = std::make_unique<Cartridge>(header, std::move(prg), std::move(chr));
    if (!cart->mapper_) {
        error = "Unsupported mapper " + std::to_string(header.mapper) + ".";
        return nullptr;
    }
    return cart;
}

Cartridge::Cartridge(Header header, std::vector<u8> prg, std::vector<u8> chr)
    : header_(header), prg_(std::move(prg)), chr_(std::move(chr)), mapper_(makeMapper(header_)) {}

bool Cartridge::cpuRead(u16 address, u8& data) {
    if (!mapper_) {
        return false;
    }
    u32 mapped = 0;
    if (mapper_->cpuMapRead(address, mapped, data)) {
        if (mapped != kMapperHandled) {
            data = prg_[mapped % prg_.size()];
        }
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(u16 address, u8 data) {
    if (!mapper_) {
        return false;
    }
    u32 mapped = 0;
    if (mapper_->cpuMapWrite(address, mapped, data)) {
        if (mapped != kMapperHandled) {
            prg_[mapped % prg_.size()] = data;
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(u16 address, u8& data) {
    if (!mapper_) {
        return false;
    }
    u32 mapped = 0;
    if (mapper_->ppuMapRead(address, mapped, data)) {
        if (mapped != kMapperHandled) {
            data = chr_[mapped % chr_.size()];
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(u16 address, u8 data) {
    if (!mapper_) {
        return false;
    }
    u32 mapped = 0;
    if (mapper_->ppuMapWrite(address, mapped, data)) {
        if (mapped != kMapperHandled) {
            chr_[mapped % chr_.size()] = data;
        }
        return true;
    }
    return false;
}

void Cartridge::reset() {
    if (mapper_) {
        mapper_->reset();
    }
}

void Cartridge::clockCpu() {
    if (mapper_) {
        mapper_->clockCpu();
    }
}

void Cartridge::scanline() {
    if (mapper_) {
        mapper_->scanline();
    }
}

void Cartridge::scanlineStart(int scanline) {
    if (mapper_) {
        mapper_->scanlineStart(scanline);
    }
}

bool Cartridge::usesPpuFetchNotifications() const {
    return mapper_ && mapper_->usesPpuFetchNotifications();
}

void Cartridge::notifyPpuFetch(PpuFetchKind kind) {
    if (mapper_) {
        mapper_->notifyPpuFetch(kind);
    }
}

bool Cartridge::irqPending() const {
    return mapper_ && mapper_->irqPending();
}

void Cartridge::clearIrq() {
    if (mapper_) {
        mapper_->clearIrq();
    }
}

u8 Cartridge::expansionAudioSample() {
    return mapper_ ? mapper_->expansionAudioSample() : 0;
}

Mirroring Cartridge::mirroring() const {
    return mapper_ ? mapper_->mirroring() : header_.mirroring;
}

std::string Cartridge::mapperName() const {
    switch (header_.mapper) {
    case 0: return "NROM";
    case 1: return "MMC1";
    case 2: return "UxROM";
    case 4: return "MMC3";
    case 5: return "MMC5";
    case 24:
    case 26: return "Konami VRC6";
    default: return "Unsupported";
    }
}

} // namespace nes
