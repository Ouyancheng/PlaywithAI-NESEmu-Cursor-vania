# Architecture and Algorithms

This document explains how the emulator components cooperate and the basic algorithms behind them. It is intended for reviewers who need to understand the code without already knowing the NES hardware.

## Runtime Loop

The frontend owns a `nes::Nes` instance. A loaded ROM becomes a `Cartridge`, the cartridge creates a mapper, and `Nes::stepFrame()` advances hardware until the PPU publishes a completed frame.

```text
Frontend draw tick
  -> Nes::stepFrame()
     -> 3 PPU clocks per CPU cycle
     -> 1 CPU clock per CPU cycle unless DMA stalls it
     -> 1 APU/mapper clock per CPU cycle
     -> latch NMI/IRQ and service only between CPU instructions
  -> upload completed framebuffer
  -> push generated audio samples
```

The timing ratio is the most important invariant: the NTSC PPU runs three times for each CPU cycle, while the APU and mapper CPU-side logic run once per CPU cycle.

## CPU

`CPU6502` is an instruction-level 6502 executor. Each opcode consumes cycles, reads and writes through `Bus`, and updates registers/flags.

Core algorithm:

1. If the current instruction has remaining cycles, decrement and return.
2. Fetch the opcode from `pc`.
3. Decode addressing mode and operation.
4. Execute memory/register changes.
5. Add base cycles and any page-cross/branch cycles.

Interrupts are latched by `Nes` and delivered only when `CPU6502::complete()` is true. This avoids interrupting a partially executed instruction. NMI/IRQ entry pushes the old processor status before setting the interrupt-disable flag, which is important for games that rely on mapper IRQs.

## Bus

`Bus` is the CPU address decoder. It routes CPU reads and writes to:

- 2 KB internal RAM mirrored through `$0000-$1FFF`.
- PPU registers mirrored through `$2000-$3FFF`.
- APU, controller, and DMA registers in `$4000-$4017`.
- The active cartridge for mapper-controlled PRG space and cartridge RAM.

OAM DMA is handled by copying 256 bytes from CPU-visible memory into PPU OAM and stalling the CPU while PPU/APU/mapper clocks continue.

## PPU

`PPU` is a scanline/dot renderer with loopy-style scroll state.

Important state:

- `vramAddress_`: current VRAM address, equivalent to the PPU `v` register.
- `tramAddress_`: temporary scroll/address register, equivalent to `t`.
- `fineX_`: fine horizontal scroll.
- `writeLatch_`: first/second write toggle for `$2005/$2006`.
- Background pattern and attribute shifters.

Background rendering algorithm:

1. Fetch nametable tile ID.
2. Fetch attribute bits for that tile quadrant.
3. Fetch low and high pattern bytes for the tile row.
4. Load those bytes into shifters every 8 dots.
5. Shift one pixel each visible dot and combine pattern bits with palette bits.

Sprite rendering first selects up to 8 sprites visible on the current scanline, then each visible pixel checks those sprites in OAM order. The first non-transparent sprite pixel wins unless background priority rules place it behind the background.

Frames are rendered into `renderFramebuffer_` and published into `framebuffer_` at vblank. That keeps frontends from presenting a half-updated frame.

## APU

`APU` generates audio at CPU-clock granularity and periodically emits 44.1 kHz samples.

Implemented channels:

- Pulse 1 and pulse 2: duty sequencer, envelope, length counter, and sweep.
- Triangle: 32-step waveform with length and linear counters.
- Noise: LFSR noise source with envelope and length counter.
- DMC: sample reader, output-level stepping, loop/IRQ status, and mixer contribution.
- Mapper expansion input, currently used by VRC6 and MMC5.

The mixer uses NES-style nonlinear pulse and TND formulas, adds mapper expansion audio, applies a low-cut DC blocker, and clamps the final sample. The DC blocker reduces quiet-state popping without intentionally smoothing high frequencies.

## Cartridge and Mappers

`Cartridge` owns PRG and CHR storage and delegates address mapping to a `Mapper`. Mappers translate CPU/PPU addresses into PRG/CHR offsets or handle the access internally using `kMapperHandled`.

Mapper responsibilities can include:

- PRG-ROM bank switching.
- CHR-ROM/CHR-RAM bank switching.
- PRG-RAM or WRAM.
- Runtime nametable mirroring.
- Scanline or CPU-cycle IRQs.
- Expansion audio.
- Mapper-owned nametable or ExRAM behavior.

The current mapper set covers common boards: NROM, MMC1, UxROM, MMC3, MMC5, and Konami VRC6 variants.

## Frontends

The macOS frontend uses AppKit for application UI, `MTKView`/Metal for display, and AudioQueue for sound. The Metal draw callback is the frame pacing point: it advances one emulation frame, uploads the completed framebuffer, and pushes audio samples.

The Win32 frontend is native Win32: GDI presents frames, `waveOut` plays audio, and a window timer advances emulation.

Both frontends deliberately keep platform code outside `nes_core`, so emulator behavior can be tested in portable C++ tests.

## Testing Philosophy

`tests/CoreTests.cpp` contains smoke and regression tests for the bugs fixed during development. These tests are not a replacement for public NES conformance ROMs, but they protect key invariants:

- CPU execution and interrupt status restoration.
- PPU frame completion and OAM DMA.
- APU frame counter and DMC sample fetching.
- Mapper banking, RAM, IRQ, and nametable behavior for supported mappers.

For future accuracy work, each hardware fix should add a small regression test when the behavior is expressible without a full ROM trace.
