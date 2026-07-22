# TestAiNES Debugging Journey

This document records the debugging path that turned TestAiNES from a basic emulator foundation into a much more playable emulator for Super Mario Bros. and Akumajou Densetsu.

The most important lesson from the journey was that emulator bugs often look like graphics bugs even when the cause is elsewhere. In this project, visible tile corruption came from PPU scroll timing, VRC6 mirroring, mapper IRQs, CPU interrupt status, and even frontend framebuffer races.

## Starting Point

The original target was ambitious:

- Native macOS GUI.
- Portable C++ NES core.
- Full 6502 CPU, PPU, and APU support.
- Mapper support for NROM, MMC1, MMC3, and Konami mapper behavior needed by Castlevania games.
- Playability for Super Mario Bros. and Akumajou Densetsu.

The first implementation established the core shape:

- `CPU6502`
- `PPU`
- `APU`
- `Bus`
- `Cartridge`
- Mapper interface
- Native macOS shell with AppKit, MetalKit, and CoreAudio

After that, the work became an iterative compatibility and accuracy pass driven by real game behavior.

## Mapper Names

Symptom:

The code initially used numeric mapper names such as `Mapper000` and `Mapper001`, which made the code hard to read.

Action:

Renamed mapper implementations to descriptive names:

- `NromMapper`
- `Mmc1Mapper`
- `Mmc3Mapper`
- `Vrc6Mapper`

Result:

The mapper layer became easier to reason about and documentation could talk about real hardware names instead of only iNES numbers.

## macOS App Would Not Launch

Symptom:

The app bundle failed to launch from Finder/open, with macOS reporting bundle/executable problems.

Investigation:

The app bundle metadata and signing were not acceptable to LaunchServices. There were also older API choices around file type selection.

Action:

- Let CMake generate the bundle `Info.plist`.
- Set bundle properties through `CMakeLists.txt`.
- Added ad-hoc code signing after build.
- Switched the open panel to `UniformTypeIdentifiers`.

Result:

The app launched normally as a native macOS application.

## Super Mario Bros. Colors, Scaling, Performance, and Audio

Symptom:

Super Mario Bros. displayed with poor colors, appeared too small on Retina displays, had sharp/weird sound, and showed unnecessary performance issues.

Actions:

- Replaced the placeholder palette with a more accurate NES palette.
- Reworked Metal presentation to render a scaled textured quad.
- Used nearest-neighbor sampling to preserve pixel art.
- Set window aspect ratio and minimum size.
- Moved APU clocking to CPU rate instead of PPU rate.
- Added NES-style nonlinear audio mixing.
- Added a light low-pass smoothing filter.
- Optimized sprite rendering by evaluating only sprites visible on each scanline.
- Added OAM DMA support through `$4014`.
- Set the default build type to an optimized configuration.

Result:

Super Mario Bros. became visually correct enough to play, scaled properly in the macOS window, and audio pitch/quality improved significantly.

## OAM DMA

Symptom:

Sprites could appear incorrect or unstable in games that rely on DMA uploads to sprite memory.

Investigation:

NES games typically write a page number to `$4014`, causing 256 bytes to be copied to PPU OAM while the CPU is stalled.

Action:

- Added `PPU::dmaWrite()`.
- Implemented `$4014` handling in `Bus`.
- Added CPU DMA stall behavior while still clocking PPU/APU/mapper devices.

Result:

Sprite data uploads became much closer to hardware behavior.

## Early Castlevania 3 Scroll Problems

Symptom:

The Akumajou Densetsu story screen scrolled incorrectly. Tiles appeared too early, and parts of the intro image were garbled before the scroll reached them.

Early Investigation:

The PPU scroll model was too simple. It treated scrolling more like a frame-level coordinate than the NES hardware's per-dot VRAM address pipeline.

Action:

Replaced the simplified scroll behavior with a Loopy-style PPU scroll model:

- `vramAddress_` as current VRAM/render address.
- `tramAddress_` as temporary address.
- `fineX_` for fine horizontal scroll.
- `writeLatch_` for `$2005/$2006` write sequencing.
- Per-dot background fetches.
- Pattern and attribute shift registers.
- Coarse X/Y increments.
- Horizontal transfer at dot 257.
- Vertical transfer during pre-render scanline dots 280-304.

Result:

The PPU moved closer to hardware behavior and fixed several classes of scroll bugs, though deeper VRC6 issues remained.

## PPU Data Read Buffering

Symptom:

Some graphics and palette behavior did not line up with game expectations.

Investigation:

`PPUDATA` reads are buffered for nametable/pattern memory but palette reads return immediately and update the internal buffer from mirrored nametable memory.

Action:

Corrected `$2007` read behavior:

- Palette reads return immediate palette data.
- Non-palette reads return the old buffer and refill it.
- Palette reads still update the buffer from the mirrored nametable region.

Result:

PPU register behavior became closer to real hardware.

## VRC6 Mapper Completeness Pass

Symptom:

Akumajou Densetsu backgrounds and stage transitions were corrupted. Some fixes improved one screen but made another screen worse.

Investigation:

Akumajou Densetsu uses Konami VRC6. The mapper affects PRG banking, CHR banking, mirroring, PRG-RAM, IRQ timing, and expansion audio. A small mistake in any of those areas can look like tile corruption.

Actions:

- Added VRC6 PRG-RAM at `$6000-$7FFF`.
- Implemented PRG-RAM enable through `$B003` bit 7.
- Added VRC6a/VRC6b A0/A1 register address-line handling.
- Implemented VRC6 IRQ latch/control/acknowledge registers.
- Implemented scanline-mode IRQ prescaler using the 341-subtract-3 model.
- Added basic VRC6 pulse and sawtooth expansion audio.
- Added smoke tests for PRG-RAM, Mapper 26 address swapping, VRC6 IRQs, and VRC6 banking behavior.

Result:

VRC6 behavior became much more complete, but the game still exposed timing and mirroring issues.

## CPU Unofficial Opcodes

Symptom:

Some game behavior still looked unstable even after mapper and PPU improvements.

Investigation:

Commercial games and diagnostics sometimes rely on unofficial 6502 opcodes. If an emulator treats these as invalid or as wrong-length instructions, the program counter can desynchronize and corrupt later PPU/mapper writes.

Action:

Implemented common stable unofficial opcodes, including:

- `LAX`
- `SAX`
- `DCP`
- `ISC`
- `SLO`
- `RLA`
- `SRE`
- `RRA`
- `ANC`
- `ALR`
- `ARR`
- `AXS`
- Common unofficial NOP variants

Result:

The CPU became more robust for commercial ROM behavior.

## Interrupt Delivery Timing

Symptom:

Mapper IRQ fixes did not fully stabilize Castlevania 3. Some screens still had wrong CHR banks or corrupted pause/gameplay states.

Investigation:

Interrupts should not be serviced in the middle of an instruction. NMI/IRQ requests need to be latched and delivered when the CPU reaches an instruction boundary.

Action:

Added pending interrupt latches in `Nes`:

- `nmiPending_`
- `irqPending_`

`Nes::stepFrame()` now samples PPU NMI and mapper IRQ requests continuously, but only calls `CPU6502::nmi()` or `CPU6502::irq()` when `cpu_.complete()` is true.

Result:

CPU/mapper/PPU interaction became more faithful, especially around scanline split IRQs.

## Pause Screen Corruption

Symptom:

After entering gameplay and pressing Start, the pause screen filled the stage with Japanese/text glyph tiles. Pressing Start again returned to a mostly coherent gameplay screen.

Investigation:

Headless reproduction showed:

- Normal gameplay frame was coherent.
- Pause frame became text/glyph-tile corrupted.
- Unpause returned to coherent gameplay.

Traces showed:

- The pause transition wrote only four nametable bytes at `$2052-$2055`, the intended `PAUSE` text.
- The game did not upload a whole corrupted nametable.
- VRC6 IRQ split behavior was not being serviced during pause.

Root Cause:

`CPU6502::irq()` and `CPU6502::nmi()` set the interrupt-disable flag before pushing processor status. Real 6502 hardware pushes the old status first, then sets `I`.

Because the emulator pushed status after setting `I`, `RTI` restored `I=1`. That could block mapper IRQs. When the VRC6 split IRQ did not run, CHR banks intended for the HUD/text region stayed active in the gameplay region.

Action:

Changed interrupt entry order:

1. Push PC high.
2. Push PC low.
3. Prepare old status with correct B/U bits.
4. Push old status.
5. Set interrupt-disable flag.
6. Jump to vector.

Added a regression test to ensure `RTI` restores the pre-interrupt `I` flag.

Result:

Pause no longer turned the stage into glyph tiles.

## Visible Left-Side Refresh While Walking

Symptom:

In the macOS app, the left side of the gameplay background appeared to refresh while the character walked and the screen scrolled horizontally.

Investigation:

Headless frame dumps were stable, but the app still showed visible refresh behavior. That suggested the completed framebuffer was correct, but presentation could read from it while the emulator was publishing or stepping another frame.

Root Cause:

The app advanced emulation using an `NSTimer`, while `MTKView` uploaded frames on its own draw cadence. The renderer read a live framebuffer reference.

Action:

- Added a mutex to `Nes`.
- Protected load/reset/step/input operations.
- Added `Nes::framebufferSnapshot()`, which returns a copied framebuffer under lock.
- Changed Metal upload to use the snapshot instead of a live reference.

Result:

The GUI no longer reads partially updated frame data. This removed the visible left-to-right refresh artifact.

## Horizontal Scrolling Nametable Issue

Symptom:

During horizontal scrolling, the left side of the gameplay background could show wrong or stale tiles. Later stage transitions could also produce severe background corruption.

Investigation:

Akumajou Densetsu uses VRC6 mode `$B003=$20`. In this mode, VRC6 documentation says mode 0 uses vertical mirroring. The emulator had mode 0 and mode 1 mirroring reversed.

Why this matters:

Horizontal scrolling needs the correct pair of nametables. If vertical/horizontal mirroring is swapped, the PPU can fetch from the wrong nametable as the camera crosses screen boundaries. This looks like one side of the screen refreshing or showing wrong tiles.

Action:

Changed VRC6 `$B003` mirroring:

- Mode 0 now maps to vertical mirroring.
- Mode 1 now maps to horizontal mirroring.

Updated the VRC6 mirroring regression test.

Result:

Headless walk-right checkpoints stayed coherent across horizontal scrolling. The game became smooth and playable.

## CHR A10 Investigation

Symptom:

VRC6 documentation also describes special CHR A10 behavior when `$B003` bit 5 is set. Enabling this behavior seemed theoretically correct but made some frames worse in this emulator.

Investigation:

We tested CHR A10 behavior separately from mirroring. The experiment made pause/non-pause consistency better in one case but corrupted other stage graphics. Combining documented mirroring with the CHR A10 change also introduced wrong right-side tiles.

Action:

Kept the VRC6 mirroring correction because it clearly fixed horizontal scrolling behavior. Reverted the unsafe CHR A10 experiment for now.

Result:

The current emulator prioritizes the behavior verified by frame dumps and gameplay. CHR A10 remains an area for future test-ROM-driven mapper work.

## Double Buffering

Symptom:

At one point, background updates appeared visible while scrolling.

Investigation:

Some writes happened in vblank, but the displayed output still looked like it could show partial frame construction.

Action:

Added PPU back-buffer rendering:

- Draw pixels into `renderFramebuffer_`.
- Publish to `framebuffer_` only at vblank.

Later, this was complemented by the GUI-side locked snapshot.

Result:

The core publishes complete frames, and the app uploads stable snapshots.

## Gameplay Music Percussion Pass

Symptom:

The gameplay background music could sound like it was missing some drum hits near the start of gameplay.

Investigation:

NES percussion often comes from the noise channel, but some games also use the DMC channel for short sampled transients. The APU already had pulse, triangle, and noise support, but it did not yet implement DMC sample playback or `$4017` frame-counter mode behavior.

Action:

- Added DMC register handling for `$4010-$4013`.
- Added DMC enable/status behavior through `$4015`.
- Added DMC sample fetching through the CPU bus address space.
- Added DMC output level stepping, looping, and IRQ status.
- Mixed DMC into the NES nonlinear TND mixer.
- Added `$4017` four-step/five-step frame-counter behavior and frame IRQ inhibit handling.
- Added regression tests for frame-counter IRQ behavior and DMC sample fetching.

Result:

The APU now covers the main hardware path used for sampled percussion and more accurate frame sequencing. Remaining audio work should focus on conformance-test details and subjective mixer balance rather than a completely missing DMC channel.

## Background Music Pop Reduction

Symptom:

After DMC support landed, background music still had occasional popping noises.

Investigation:

The first pop-reduction attempt used stronger output filtering and de-click limiting. It reduced discontinuities, but it colored the VRC6 music too much and made the gameplay melody sound like it had shifted scale. The safer conclusion was that filtering should not be used to hide fundamental artifacts. Pops during quiet moments are more likely to come from DC level changes or audio callback underruns than from missing treble filtering.

Action:

- Replaced broad smoothing with a low-cut DC blocker plus a high-frequency-preserving low-pass stage.
- Reduced the overall output gain enough to avoid clipping.
- Reduced the VRC6 expansion contribution only moderately, without changing its pitch or waveform timing.
- Changed CoreAudio underrun behavior to decay from the last sample instead of jumping immediately to zero.
- Added a short underrun recovery ramp so audio resumes from the decayed value instead of stepping abruptly.

Result:

The generated gameplay audio stream has no clipping in the tested path while preserving more high-frequency content than the earlier smoothing pass. Percussion remains present, and sharp pops from clipping, DC shifts, and underrun recovery are reduced without the aggressive filtering that made the melody sound wrong.

Follow-up:

The first conservative smoothing value still filtered too much high-frequency content and made some BGM sound overly smooth. That led to replacing the smoothing-centered approach with the current DC-blocking approach, which targets quiet-state pops without intentionally dulling the musical harmonics.

After confirming that quiet-state popping was gone, the output low-pass cutoff was raised from 14 kHz to 18 kHz so more high-frequency pulse/VRC6 harmonics can pass through. The DC blocker and AudioQueue underrun ramp remain responsible for pop prevention.

## Regression Tests Added During the Journey

The test suite gained coverage for the bugs we found:

- CPU interrupt status restoration after `RTI`.
- APU frame-counter IRQ behavior.
- APU DMC sample fetching and status.
- OAM DMA smoke test.
- VRC6 PRG-RAM enable/read/write.
- VRC6 Mapper 26 address-line swap.
- VRC6 IRQ smoke test.
- VRC6 mirroring behavior.
- VRC6 pattern and ROM nametable smoke checks.

These tests are intentionally small. They are designed to prevent known regressions while still keeping the build fast.

## Debugging Techniques That Helped

The most useful techniques were:

- Headless frame dumping to PNG/PPM checkpoints.
- Reproducing user input sequences in scripts.
- Comparing normal gameplay, pause, and unpause frames.
- Tracing mapper writes around the exact failure frame.
- Tracing PPU `$2005/$2006/$2007` writes to separate scroll bugs from nametable upload bugs.
- Checking real hardware documentation before changing mapper logic.
- Testing one hypothesis at a time and reverting changes that improved one symptom but made another screen worse.

## Final State After This Debugging Pass

The emulator now:

- Launches as a native macOS app.
- Runs Super Mario Bros. much better than the initial foundation.
- Runs Akumajou Densetsu smoothly enough for normal gameplay.
- Handles the tested pause-state VRC6 split correctly.
- Handles horizontal scrolling much more cleanly.
- Avoids GUI framebuffer race artifacts.

There are still glitches, but the remaining issues no longer block overall gameplay in the tested path.

## Future Debugging Advice

When another visual glitch appears, avoid assuming it is only a PPU renderer bug. Check in this order:

1. Is the core framebuffer correct in a headless dump?
2. If headless is correct, suspect frontend presentation or synchronization.
3. If headless is wrong, trace PPU register writes around the failing frame.
4. If CHR banks change mid-frame, trace mapper IRQ timing.
5. If IRQ timing looks wrong, verify CPU interrupt flags and instruction-boundary delivery.
6. If scrolling wraps incorrectly, verify mapper mirroring before changing nametable data logic.
7. If a hardware-documented change makes a known screen worse, keep it isolated and look for the missing interacting behavior before landing it.
