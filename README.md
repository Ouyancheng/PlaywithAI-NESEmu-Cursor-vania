# TestAiNES

TestAiNES is a greenfield NES emulator foundation written in C++20 with a native macOS frontend. The emulator core is portable C++; the app shell uses AppKit for the window/menu, MetalKit for video presentation, and CoreAudio AudioQueue for sound output.

## Build

### macOS

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
open build/TestAiNES.app
```

CMake exports `build/compile_commands.json` for IDEs and language servers.

### Windows

Use a Visual Studio Developer PowerShell or another environment with a Windows C++ toolchain:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\TestAiNES.exe
```

The Windows frontend is native Win32: GDI for video presentation, `waveOut` for audio, Win32 file dialogs, menus, timers, and keyboard input.

### Testing Windows From macOS

The Win32 frontend cannot be run directly on macOS without a compatibility layer or VM. Practical options:

- Build and run on a Windows machine or VM.
- Add a GitHub Actions `windows-latest` job to compile and run `nes_core_tests`.
- Cross-compile from macOS with MinGW/LLVM-mingw if installed, then smoke-test under Wine if available.
- Keep all emulator logic in `nes_core` so macOS tests continue to validate the portable core.

## Controls

- `Z`: A
- `X`: B
- `A`: Select
- `S`: Start
- Arrow keys: D-pad
- `Cmd+O`: Open `.nes` ROM
- `Cmd+R`: Reset

## Documentation

- [Architecture and Algorithms](docs/ARCHITECTURE_AND_ALGORITHMS.md): reviewer-oriented overview of component responsibilities and core algorithms.
- [Emulator Modules](docs/EMULATOR_MODULES.md): architecture and module-by-module explanation.
- [Debugging Journey](docs/DEBUGGING_JOURNEY.md): symptom-to-fix log from the Super Mario and Akumajou Densetsu compatibility work.

## Emulator Status

Implemented:

- iNES ROM loading.
- 6502 official opcode dispatcher with stack, interrupts, flags, addressing modes, cycle accounting, and common unofficial opcodes.
- CPU bus with RAM mirroring, PPU/APU register routing, cartridge access, OAM DMA, and controller serial input.
- PPU register model, loopy scroll timing, nametable/palette memory, scanline timing, vblank/NMI, background fetch shifters, sprite rendering, sprite-zero hit, and double-buffered frame publishing.
- APU pulse, triangle, noise, and DMC sample generation with CoreAudio playback and VRC6 expansion audio mixing.
- Mappers: NROM, MMC1, UxROM, MMC3, MMC5, Konami VRC6a, and Konami VRC6b.
- Native macOS GUI with ROM picker, display-linked Metal presentation, keyboard input, reset, and AudioQueue playback.

Compatibility work that still needs test-ROM-driven iteration:

- PPU edge cases, exact sprite overflow behavior, and broader rendering conformance.
- More exact APU frame counter edge behavior, DMC edge cases, and exact mixer behavior.
- MMC3 A12 IRQ edge timing plus deeper MMC5 and Konami VRC6 conformance tests.
- Save RAM persistence and NES 2.0 metadata.

## Recommended Validation ROMs

Use public test ROMs to tighten behavior before judging game compatibility:

- `nestest.nes` for CPU instruction tracing.
- Blargg CPU interrupt and instruction tests.
- Blargg PPU timing, sprite, and palette tests.
- Blargg APU tests.
- Mapper-specific MMC1/UxROM/MMC3/MMC5/VRC6 test ROMs where available.

The current milestone is a playable emulator for the tested Super Mario and Akumajou Densetsu paths, not a mature compatibility claim. Super Mario and Castlevania games should continue to be used as regression targets while closing the remaining timing, audio, and mapper gaps.
