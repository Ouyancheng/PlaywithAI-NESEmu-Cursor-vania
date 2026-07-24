#include "core/Bus.hpp"
#include "core/CPU6502.hpp"
#include "core/Cartridge.hpp"
#include "core/Nes.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::vector<nes::u8> makeProgramRom() {
    std::vector<nes::u8> prg(32768, 0xea);
    prg[0x0000] = 0xa9;
    prg[0x0001] = 0x42;
    prg[0x0002] = 0xaa;
    prg[0x0003] = 0xe8;
    prg[0x0004] = 0x00;
    prg[0x7ffc] = 0x00;
    prg[0x7ffd] = 0x80;
    prg[0x7ffe] = 0x04;
    prg[0x7fff] = 0x80;
    return prg;
}

void runCpuSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 0;
    header.prgBanks = 2;
    header.chrBanks = 0;
    header.mirroring = nes::Mirroring::Horizontal;
    nes::Cartridge cartridge(header, makeProgramRom(), std::vector<nes::u8>(8192));
    nes::Bus bus;
    nes::CPU6502 cpu;
    bus.insertCartridge(&cartridge);
    cpu.connect(&bus);
    bus.reset();
    cpu.reset();
    for (int i = 0; i < 40; ++i) {
        cpu.clock();
        for (int p = 0; p < 3; ++p) {
            bus.ppu().clock();
        }
    }
    assert(cpu.a == 0x42);
    assert(cpu.x == 0x43);
}

void runCpuInterruptStatusSmokeTest() {
    auto prg = makeProgramRom();
    prg[0x0000] = 0x58; // CLI
    prg[0x0001] = 0xea; // NOP
    prg[0x1000] = 0x40; // RTI at $9000
    prg[0x7ffa] = 0x00;
    prg[0x7ffb] = 0x90;

    nes::Cartridge::Header header;
    header.mapper = 0;
    header.prgBanks = 2;
    header.chrBanks = 0;
    header.mirroring = nes::Mirroring::Horizontal;
    nes::Cartridge cartridge(header, std::move(prg), std::vector<nes::u8>(8192));
    nes::Bus bus;
    nes::CPU6502 cpu;
    bus.insertCartridge(&cartridge);
    cpu.connect(&bus);
    bus.reset();
    cpu.reset();

    while (!cpu.complete()) cpu.clock();
    cpu.clock(); // CLI
    while (!cpu.complete()) cpu.clock();
    assert((cpu.status & 0x04) == 0);

    cpu.nmi();
    while (!cpu.complete()) cpu.clock();
    cpu.clock(); // RTI
    while (!cpu.complete()) cpu.clock();
    assert((cpu.status & 0x04) == 0);
}

void runCartridgeLoadTest() {
    const auto path = std::filesystem::temp_directory_path() / "testaines_smoke.nes";
    std::vector<nes::u8> file;
    file.insert(file.end(), {'N', 'E', 'S', 0x1a, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    auto prg = makeProgramRom();
    file.insert(file.end(), prg.begin(), prg.end());
    file.resize(file.size() + 8192, 0);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
    std::string error;
    auto cartridge = nes::Cartridge::loadFromFile(path, error);
    assert(cartridge);
    assert(cartridge->header().mapper == 0);
    assert(cartridge->mapperName() == "NROM");
    std::filesystem::remove(path);
}

void runNesFrameSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 0;
    header.prgBanks = 2;
    header.chrBanks = 0;
    nes::Cartridge cartridge(header, makeProgramRom(), std::vector<nes::u8>(8192));
    nes::Bus bus;
    bus.insertCartridge(&cartridge);
    bus.reset();
    for (int i = 0; i < 341 * 262; ++i) {
        bus.ppu().clock();
    }
    assert(bus.ppu().frameComplete());
}

void runOamDmaSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 0;
    header.prgBanks = 2;
    header.chrBanks = 0;
    nes::Cartridge cartridge(header, makeProgramRom(), std::vector<nes::u8>(8192));
    nes::Bus bus;
    bus.insertCartridge(&cartridge);
    bus.reset();
    bus.cpuWrite(0x0200, 0x24);
    bus.cpuWrite(0x0201, 0x42);
    bus.cpuWrite(0x0202, 0x03);
    bus.cpuWrite(0x0203, 0x80);
    bus.cpuWrite(0x4014, 0x02);
    bus.cpuWrite(0x2003, 0x00);
    assert(bus.cpuRead(0x2004) == 0x24);
    bus.cpuWrite(0x2003, 0x01);
    assert(bus.cpuRead(0x2004) == 0x42);
}

void runApuFrameCounterSmokeTest() {
    nes::APU apu;
    apu.reset();
    apu.cpuWrite(0x4017, 0x00);
    for (int i = 0; i < 29829; ++i) {
        apu.clock();
    }
    assert((apu.cpuRead(0x4015) & 0x40) != 0);
    assert((apu.cpuRead(0x4015) & 0x40) == 0);

    apu.cpuWrite(0x4017, 0xc0);
    for (int i = 0; i < 37281; ++i) {
        apu.clock();
    }
    assert((apu.cpuRead(0x4015) & 0x40) == 0);
}

void runApuDmcFetchSmokeTest() {
    nes::APU apu;
    apu.reset();
    int reads = 0;
    nes::u16 lastAddress = 0;
    apu.setDmcReader([&](nes::u16 address) {
        ++reads;
        lastAddress = address;
        return static_cast<nes::u8>(0xff);
    });

    apu.cpuWrite(0x4010, 0x80);
    apu.cpuWrite(0x4012, 0x02);
    apu.cpuWrite(0x4013, 0x00);
    apu.cpuWrite(0x4015, 0x10);
    assert((apu.cpuRead(0x4015) & 0x10) != 0);

    apu.clock();
    assert(reads == 1);
    assert(lastAddress == 0xc080);
    assert((apu.cpuRead(0x4015) & 0x90) == 0x80);
}

void writeMmc1Register(nes::Cartridge& cartridge, nes::u16 address, nes::u8 value) {
    for (int i = 0; i < 5; ++i) {
        cartridge.cpuWrite(address, static_cast<nes::u8>((value >> i) & 1));
    }
}

void runMmc1PrgRamAndLargeBankSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 1;
    header.prgBanks = 32;
    header.chrBanks = 0;

    std::vector<nes::u8> prg(header.prgBanks * 16 * 1024);
    prg[15 * 0x4000] = 0x15;
    prg[31 * 0x4000] = 0x31;
    nes::Cartridge cartridge(header, std::move(prg), std::vector<nes::u8>(8192));

    nes::u8 data = 0;
    cartridge.cpuWrite(0x6002, 0x5a);
    assert(cartridge.cpuRead(0x6002, data));
    assert(data == 0x5a);

    writeMmc1Register(cartridge, 0xe000, 0x10);
    assert(cartridge.cpuRead(0x6002, data));
    assert(data == 0xff);
    cartridge.cpuWrite(0x6002, 0x33);
    writeMmc1Register(cartridge, 0xe000, 0x00);
    assert(cartridge.cpuRead(0x6002, data));
    assert(data == 0x5a);

    assert(cartridge.cpuRead(0xc000, data));
    assert(data == 0x15);
    writeMmc1Register(cartridge, 0xa000, 0x10);
    assert(cartridge.cpuRead(0xc000, data));
    assert(data == 0x31);
}

void runMmc3PrgRamAndMirroringSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 4;
    header.prgBanks = 4;
    header.chrBanks = 1;
    header.mirroring = nes::Mirroring::Vertical;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::vector<nes::u8>(8192));

    nes::u8 data = 0;
    cartridge.cpuWrite(0x6000, 0x66);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x66);

    cartridge.cpuWrite(0xa001, 0xc0);
    cartridge.cpuWrite(0x6000, 0x77);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x66);

    cartridge.cpuWrite(0xa001, 0x00);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0xff);
    cartridge.cpuWrite(0xa001, 0x80);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x66);

    cartridge.cpuWrite(0xa000, 0x01);
    assert(cartridge.mirroring() == nes::Mirroring::Horizontal);

    header.mirroring = nes::Mirroring::FourScreen;
    nes::Cartridge fourScreen(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::vector<nes::u8>(8192));
    fourScreen.cpuWrite(0xa000, 0x01);
    assert(fourScreen.mirroring() == nes::Mirroring::FourScreen);
}

void runUxromBankSwitchAndChrRamSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 2;
    header.prgBanks = 4;
    header.chrBanks = 0;
    header.mirroring = nes::Mirroring::Vertical;

    std::vector<nes::u8> prg(header.prgBanks * 16 * 1024);
    prg[0 * 0x4000] = 0x10;
    prg[2 * 0x4000] = 0x32;
    prg[3 * 0x4000] = 0xff;
    nes::Cartridge cartridge(header, std::move(prg), std::vector<nes::u8>(8192));

    nes::u8 data = 0;
    assert(cartridge.mapperName() == "UxROM");
    assert(cartridge.mirroring() == nes::Mirroring::Vertical);
    assert(cartridge.cpuRead(0x8000, data));
    assert(data == 0x10);
    assert(cartridge.cpuRead(0xc000, data));
    assert(data == 0xff);

    cartridge.cpuWrite(0x8000, 0x02);
    assert(cartridge.cpuRead(0x8000, data));
    assert(data == 0x32);
    assert(cartridge.cpuRead(0xc000, data));
    assert(data == 0xff);

    assert(cartridge.ppuWrite(0x0010, 0x7b));
    assert(cartridge.ppuRead(0x0010, data));
    assert(data == 0x7b);
}

void runMmc5SmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 5;
    header.prgBanks = 4;
    header.chrBanks = 16;
    header.mirroring = nes::Mirroring::Vertical;

    std::vector<nes::u8> prg(header.prgBanks * 16 * 1024);
    for (int bank = 0; bank < header.prgBanks * 2; ++bank) {
        prg[bank * 0x2000] = static_cast<nes::u8>(0x80 + bank);
    }
    std::vector<nes::u8> chr(header.chrBanks * 8 * 1024);
    chr[3 * 0x400] = 0x33;
    chr[4 * 0x400] = 0x44;
    chr[0x43 * 0x400] = 0x43;
    nes::Cartridge cartridge(header, std::move(prg), std::move(chr));

    nes::u8 data = 0;
    assert(cartridge.mapperName() == "MMC5");

    cartridge.cpuWrite(0x5100, 0x03);
    cartridge.cpuWrite(0x5114, 0x82);
    assert(cartridge.cpuRead(0x8000, data));
    assert(data == 0x82);
    assert(cartridge.cpuRead(0xe000, data));
    assert(data == 0x87);

    cartridge.cpuWrite(0x5100, 0x00);
    cartridge.cpuWrite(0x5117, 0x84);
    assert(cartridge.cpuRead(0x8000, data));
    assert(data == 0x84);

    cartridge.cpuWrite(0x6000, 0x12);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x00);
    cartridge.cpuWrite(0x5102, 0x02);
    cartridge.cpuWrite(0x5103, 0x01);
    cartridge.cpuWrite(0x6000, 0x34);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x34);

    cartridge.cpuWrite(0x5101, 0x03);
    cartridge.cpuWrite(0x5123, 0x03);
    assert(cartridge.ppuRead(0x0c00, data));
    assert(data == 0x33);
    cartridge.cpuWrite(0x512b, 0x43);
    cartridge.notifyPpuFetch(nes::PpuFetchKind::BackgroundPattern);
    assert(cartridge.ppuRead(0x0c00, data));
    assert(data == 0x43);
    cartridge.notifyPpuFetch(nes::PpuFetchKind::SpritePattern);

    cartridge.cpuWrite(0x5105, 0x02);
    assert(cartridge.ppuWrite(0x2005, 0x6a));
    assert(cartridge.cpuRead(0x5c05, data));
    assert(data == 0x6a);
    cartridge.cpuWrite(0x5105, 0x03);
    cartridge.cpuWrite(0x5106, 0x77);
    cartridge.cpuWrite(0x5107, 0x02);
    assert(cartridge.ppuRead(0x2000, data));
    assert(data == 0x77);
    assert(cartridge.ppuRead(0x23c0, data));
    assert(data == 0x02);

    cartridge.cpuWrite(0x5205, 9);
    cartridge.cpuWrite(0x5206, 7);
    assert(cartridge.cpuRead(0x5205, data));
    assert(data == 63);
    assert(cartridge.cpuRead(0x5206, data));
    assert(data == 0);

    cartridge.cpuWrite(0x5203, 3);
    cartridge.cpuWrite(0x5204, 0x80);
    cartridge.scanlineStart(2);
    assert(!cartridge.irqPending());
    cartridge.scanlineStart(3);
    assert(cartridge.irqPending());
    assert(cartridge.cpuRead(0x5204, data));
    assert((data & 0x80) != 0);
    assert(!cartridge.irqPending());

    cartridge.cpuWrite(0x5000, 0xcf);
    cartridge.cpuWrite(0x5002, 0x01);
    cartridge.cpuWrite(0x5003, 0x00);
    cartridge.cpuWrite(0x5015, 0x01);
    assert(cartridge.expansionAudioSample() > 0);
    cartridge.cpuWrite(0x5011, 0x40);
    assert(cartridge.expansionAudioSample() > 0);
}

void runVrc6MirroringSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 24;
    header.prgBanks = 16;
    header.chrBanks = 16;
    header.mirroring = nes::Mirroring::Vertical;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::vector<nes::u8>(header.chrBanks * 8 * 1024));
    assert(cartridge.mirroring() == nes::Mirroring::Vertical);
    cartridge.cpuWrite(0xb003, 0x00);
    assert(cartridge.mirroring() == nes::Mirroring::Vertical);
    cartridge.cpuWrite(0xb003, 0x04);
    assert(cartridge.mirroring() == nes::Mirroring::Horizontal);
    cartridge.cpuWrite(0xb003, 0x08);
    assert(cartridge.mirroring() == nes::Mirroring::SingleScreenLower);
    cartridge.cpuWrite(0xb003, 0x0c);
    assert(cartridge.mirroring() == nes::Mirroring::SingleScreenUpper);
}

void runVrc6PatternBankingStyleSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 24;
    header.prgBanks = 16;
    header.chrBanks = 16;
    std::vector<nes::u8> chr(header.chrBanks * 8 * 1024);
    chr[4 * 0x400] = 0x44;
    chr[5 * 0x400] = 0x55;
    chr[6 * 0x400] = 0x66;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::move(chr));
    cartridge.cpuWrite(0xd000, 4);
    cartridge.cpuWrite(0xd001, 5);
    cartridge.cpuWrite(0xb003, 0x20);

    nes::u8 data = 0;
    assert(cartridge.ppuRead(0x0000, data));
    assert(data == 0x44);
    assert(cartridge.ppuRead(0x0400, data));
    assert(data == 0x55);

    assert(cartridge.ppuRead(0x0800, data));
    assert(data == 0x66);

    cartridge.cpuWrite(0xd001, 4);
    cartridge.cpuWrite(0xb003, 0x21);
    assert(cartridge.ppuRead(0x0000, data));
    assert(data == 0x44);
    assert(cartridge.ppuRead(0x0400, data));
    assert(data == 0x55);
}

void runVrc6RomNametableSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 24;
    header.prgBanks = 16;
    header.chrBanks = 16;
    std::vector<nes::u8> chr(header.chrBanks * 8 * 1024);
    chr[11 * 0x400] = 0x6b;
    chr[13 * 0x400] = 0x7d;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::move(chr));
    cartridge.cpuWrite(0xe002, 10);
    cartridge.cpuWrite(0xe003, 12);

    nes::u8 data = 0;
    cartridge.cpuWrite(0xb003, 0x20);
    assert(!cartridge.ppuRead(0x2000, data));
    cartridge.cpuWrite(0xb003, 0x3c);
    assert(cartridge.ppuRead(0x2000, data));
    assert(data == 0x6b);
    assert(cartridge.ppuRead(0x2400, data));
    assert(data == 0x7d);
}

void runVrc6PrgRamSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 24;
    header.prgBanks = 16;
    header.chrBanks = 16;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::vector<nes::u8>(header.chrBanks * 8 * 1024));

    cartridge.cpuWrite(0x6000, 0x12);
    nes::u8 data = 0;
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0xff);

    cartridge.cpuWrite(0xb003, 0x80);
    cartridge.cpuWrite(0x6000, 0x34);
    assert(cartridge.cpuRead(0x6000, data));
    assert(data == 0x34);
}

void runVrc6Mapper26AddressSwapSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 26;
    header.prgBanks = 16;
    header.chrBanks = 16;
    std::vector<nes::u8> chr(header.chrBanks * 8 * 1024);
    chr[5 * 0x400] = 0x5a;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::move(chr));

    cartridge.cpuWrite(0xd002, 5);
    nes::u8 data = 0;
    assert(cartridge.ppuRead(0x0400, data));
    assert(data == 0x5a);
}

void runVrc6IrqSmokeTest() {
    nes::Cartridge::Header header;
    header.mapper = 24;
    header.prgBanks = 16;
    header.chrBanks = 16;
    nes::Cartridge cartridge(header, std::vector<nes::u8>(header.prgBanks * 16 * 1024), std::vector<nes::u8>(header.chrBanks * 8 * 1024));

    cartridge.cpuWrite(0xf000, 0xfe);
    cartridge.cpuWrite(0xf001, 0x06);
    cartridge.clockCpu();
    assert(!cartridge.irqPending());
    cartridge.clockCpu();
    assert(cartridge.irqPending());
    cartridge.clockCpu();
    assert(cartridge.irqPending());
    cartridge.cpuWrite(0xf002, 0x00);
    assert(!cartridge.irqPending());
}

} // namespace

int main() {
    runCpuSmokeTest();
    runCpuInterruptStatusSmokeTest();
    runCartridgeLoadTest();
    runNesFrameSmokeTest();
    runOamDmaSmokeTest();
    runApuFrameCounterSmokeTest();
    runApuDmcFetchSmokeTest();
    runMmc1PrgRamAndLargeBankSmokeTest();
    runMmc3PrgRamAndMirroringSmokeTest();
    runUxromBankSwitchAndChrRamSmokeTest();
    runMmc5SmokeTest();
    runVrc6MirroringSmokeTest();
    runVrc6PatternBankingStyleSmokeTest();
    runVrc6RomNametableSmokeTest();
    runVrc6PrgRamSmokeTest();
    runVrc6Mapper26AddressSwapSmokeTest();
    runVrc6IrqSmokeTest();
    std::cout << "nes_core_tests passed\n";
    return 0;
}
