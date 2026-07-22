# TestAiNES

TestAiNES is a greenfield NES emulator foundation written in C++20 with a native macOS frontend. The emulator core is portable C++; the app shell uses AppKit for the window/menu, MetalKit for video presentation, and CoreAudio AudioQueue for sound output.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
open build/TestAiNES.app
```

## Controls

- `Z`: A
- `X`: B
- `A`: Select
- `S`: Start
- Arrow keys: D-pad
- `Cmd+O`: Open `.nes` ROM
- `Cmd+R`: Reset

## Documentation

- [Emulator Modules](docs/EMULATOR_MODULES.md): architecture and module-by-module explanation.
- [Debugging Journey](docs/DEBUGGING_JOURNEY.md): symptom-to-fix log from the Super Mario and Akumajou Densetsu compatibility work.

## Emulator Status

Implemented:

- iNES ROM loading.
- 6502 official opcode dispatcher with stack, interrupts, flags, addressing modes, cycle accounting, and common unofficial opcodes.
- CPU bus with RAM mirroring, PPU/APU register routing, cartridge access, OAM DMA, and controller serial input.
- PPU register model, loopy scroll timing, nametable/palette memory, scanline timing, vblank/NMI, background fetch shifters, sprite rendering, sprite-zero hit, and double-buffered frame publishing.
- APU pulse, triangle, and noise sample generation with CoreAudio playback and VRC6 expansion audio mixing.
- Mappers: NROM, MMC1, MMC3, Konami VRC6a, and Konami VRC6b.
- Native macOS GUI with ROM picker, Metal display, keyboard input, reset, audio lifecycle, and locked framebuffer snapshots for stable presentation.

Compatibility work that still needs test-ROM-driven iteration:

- PPU edge cases, exact sprite overflow behavior, and broader rendering conformance.
- DMC audio, more exact APU frame counter edge behavior, and exact mixer behavior.
- MMC3 A12 IRQ edge timing and deeper Konami VRC6 conformance tests.
- Save RAM persistence and NES 2.0 metadata.

## Recommended Validation ROMs

Use public test ROMs to tighten behavior before judging game compatibility:

- `nestest.nes` for CPU instruction tracing.
- Blargg CPU interrupt and instruction tests.
- Blargg PPU timing, sprite, and palette tests.
- Blargg APU tests.
- Mapper-specific MMC1/MMC3/VRC6 test ROMs where available.

The current milestone is a playable emulator for the tested Super Mario and Akumajou Densetsu paths, not a mature compatibility claim. Super Mario and Castlevania games should continue to be used as regression targets while closing the remaining timing, audio, and mapper gaps.
