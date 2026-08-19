# MicroBASIC

A "new" 1980s microcomputer: the [Xteink X4](https://github.com/fperuzzo72/MicroWriter#hardware)
booting straight into a text-screen BASIC environment — line editor, `RUN`,
and an MSX-style `SCREEN` command to switch between four text densities
and a full graphics mode (`PSET`/`LINE`/`CIRCLE`) — instead of a
note-taking app.

`editor/` builds and flashes today, and the BASIC is usable — see
"Status" below. This repo holds the firmware itself plus the research
and decisions that got it here — see
[docs/DEVELOPMENT_LOG.md](docs/DEVELOPMENT_LOG.md) for the full narrative
(useful for picking this project back up on a machine that doesn't have
the conversation history that produced it).

## Status

`editor/` — started as a **plain copy** of
[MicroWriter](https://github.com/fperuzzo72/MicroWriter)'s editor firmware
(not a fork/submodule, not kept in sync with upstream — see `NOTICE.md`
for exactly which commit it was copied from), now with a real BASIC
environment wired in on top of it. The device **boots straight into
MicroBASIC** — a scrolling character terminal running
[Stefan Lenz's IoT BASIC](https://github.com/slviajero/tinybasic) — with
MicroWriter's prose editor still available from the Home menu. Physical
Back always exits to the menu, the fallback for when a keyboard isn't
paired yet.

The interpreter owns the language: program storage, `LIST`, `RUN`, `NEW`,
`SAVE`/`LOAD`, `CLS`, `CATALOG`, `FOR`/`NEXT`, `GOSUB`, `DATA`/`READ`,
string and numeric variables, and direct-mode execution of anything typed
without a line number. Four things are intercepted by the firmware because
they are the *device's* and not the language's: `MENU`/`EXIT` (leave the
terminal), `VC` (a full-screen program picker), `SCREEN n` (the text modes
below), and `FILES`/`DIR` (aliases for `CATALOG`).

`GET`, `@A` and `@C` read the keyboard without blocking, `INPUT` reads a
line, and `LOCATE` positions the cursor — so a program can repaint in
place instead of scrolling. `examples/pacman.bas` exercises all of it and
is the standing hardware test. `SCREEN 4` (graphics) doesn't exist yet.

Builds clean with PlatformIO (`cd editor && pio run -e xteink_x4`) and has
been flashed and tested on the actual X4 hardware throughout. See
[docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) for what has been checked
on the device and what is still open.

## Plan so far

- **Base**: [MicroWriter](https://github.com/fperuzzo72/MicroWriter)'s
  editor firmware — BLE keyboard host, e-ink rendering/refresh pipeline
  (`GfxRenderer`/`EInkDisplay`, including a non-blocking `FAST_REFRESH` and
  an experimental windowed/partial refresh), SD card, webserver/OTA —
  copied into `editor/` here (see "Status" above), with the text editor
  being swapped out for a BASIC line editor + interpreter over time. No
  ongoing sync with MicroWriter upstream — its own future changes aren't
  expected to be relevant to MicroBASIC's direction; anything that turns
  out useful later gets ported over deliberately instead.
- **Interpreter**: [Stefan Lenz's IoT BASIC](https://github.com/slviajero/tinybasic)
  — line-numbered, tokenised, no `malloc` (its program memory is one static
  array), and closer to a 1980s BASIC than anything else that fits. It is
  **not vendored**: `patches/tinybasic/fetch.sh` pulls a pinned commit and
  four scripts patch it, so what this repo carries is the *difference* from
  upstream rather than a copy that quietly drifts. See
  [patches/tinybasic/README.md](patches/tinybasic/README.md).

  What the patches do is small on purpose — configure the language flags,
  rename its `setup()`/`loop()` so they don't collide with the firmware's,
  give it C linkage, and set build flags. The runtime it is written against
  is ours: `editor/src/tb_runtime.cpp` maps its 76-symbol contract onto the
  character terminal in `screen_editor.cpp` and the SD card, and
  `tb_bridge.cpp` hands it whole typed lines. Roughly half that contract is
  peripherals this device doesn't have and is stubbed.

  An earlier version of this project used
  [My-Basic](https://github.com/paladin-t/my_basic) and is gone. It was a
  good embedding library and the wrong language: alphabetic labels instead
  of line numbers, which meant maintaining a separate numbered program
  store and rewriting it at `RUN` time. The development log records the
  swap and why.

  [PicoBB](https://github.com/Memotech-Bill/PicoBB) (BBC BASIC for RP2040)
  remains an architecture reference, not a dependency — its
  `framebuf.c`/`vducmd.c` split (hardware-agnostic line/circle/triangle/
  flood-fill on top of three primitives) is the model for the graphics
  layer once `SCREEN 4` gets built, bound by hand to the existing
  `GfxRenderer`/`EInkDisplay` rather than to any interpreter's own graphics
  backend, to avoid duplicating the e-ink-tuned refresh pipeline
  MicroWriter already has.
- **SD card layout**: this repo's own folder name, `/MicroBASIC/`, is also
  the on-device SD card folder MicroBASIC will use for its own files
  (programs, settings) — same convention the dual-boot reader siblings
  (CrossPoint/CrossInk/CPR-vCodex) already follow, so one SD card can carry
  MicroBASIC alongside those and MicroWriter without swapping cards or
  files colliding.

## SCREEN modes — decided

| Mode | Columns × Rows | Font | Cell | Notes |
|---|---|---|---|---|
| `SCREEN 0` | 32×10 | Unscii | 24x48 | |
| `SCREEN 1` | 48×15 | Unscii | 16x32 | **boots here by default** |
| `SCREEN 2` | 64×20 | Unscii | 12x24 | |
| `SCREEN 3` | 80×24 | Unscii | 10x20 | uses the full 800×480 panel, no margin either axis |
| `SCREEN 4` | — | — | — | graphics mode, full panel — not designed yet |

**Default font: Unscii**, at all four text sizes — chosen after a
multi-round visual comparison on the physical X4 (see the development log
for the losing candidates and why). **Terminus Bold** is kept as a future
settings-menu alternative, rendered at the same four column targets (also
in `research/fonts/previews/`).

**Column math**: at `SCREEN 3` (10px-wide cells), 80 columns exactly fills
the 800px panel width, no margin. Every mode below that — `SCREEN 0/1/2`,
whose cell widths are 24/16/12px — lands on exactly 768px of content
(24×32 = 16×48 = 12×64 = 768), 32px short of the full 800px. That leftover
is split evenly as a **16px blank margin on each side** — centered, no
border drawn, just empty panel.

**Row math**: no margin needed on this axis, ever — the panel's 480px
height divides evenly by all four cell heights (20/24/32/48px), landing
on exactly 24/20/15/10 rows with zero pixels left over in every single
mode. Full height used top to bottom regardless of `SCREEN` mode.

Both are implemented: `SCREEN 0/1/2/3` all exist as real fonts
(`editor/lib/EpdFont/builtinFonts/`) and `SCREEN n` switches between them
live as a BASIC statement (clearing the terminal, since old content
wouldn't line up under a different cell grid). Only `SCREEN 4` (graphics)
remains unbuilt.

**Default boot state**: `SCREEN 1` (48 columns), in **landscape,
counter-clockwise** orientation — which is the e-ink panel's native
physical orientation (`GfxRenderer::Orientation::LandscapeCounterClockwise`
in the CPR-vCodex/MicroWriter lineage: `phyX=x, phyY=y`, no rotation
transform needed), chosen specifically so the X4's physical side buttons
land at the **top** of the screen.

## `research/fonts/`

- `previews/` — the 8 decided font/size combinations (4 Unscii + 4
  Terminus Bold), one 480x800 1-bit BMP each, ready to copy onto the
  device's SD card and view with CPR-vCodex's image viewer. **480x800
  (portrait)**, not the panel's native 800x480 — `BmpViewerActivity`
  displays at `renderer.getScreenWidth/Height()`, which is
  orientation-aware and defaults to `Portrait` (480x800 logical); a
  native-panel-resolution landscape BMP gets shrunk to fit instead of
  shown 1:1. Each was rendered at 800x480 first, then rotated 90°
  clockwise — matching the exact transform
  `GfxRenderer::rotateCoordinates()` uses for `Portrait`.

  Each carries a **character-cell ruler** for measuring columns/rows
  directly off the image: row 1 is a column ruler (numbers right-aligned
  to the column they mark, every 5 columns starting at 5); every row below
  that gets a 2-digit row number (rows 2..N — row 1 is the ruler, not
  counted), content shifted right to match. The font's name, target column
  count, and cell size print as the first line of content.

  (These preview images intentionally use the *full* grid a given cell
  size allows — e.g. 33 columns for the 24px-wide "32-column" Unscii size —
  to show as much of the font as possible for comparison. That's different
  from the actual `SCREEN` mode behavior above, which centers exactly N
  columns with margin either side.)

- `tools/generate_screen_fonts.py` — regenerates every file in
  `previews/` from `src/`. Self-contained (only needs Pillow), and its
  module docstring has the full story of *why* the Unscii resize works
  the way it does — worth reading before touching it.
- `src/` — the font source files (BDF/hex/TTF) referenced above, plus each
  family's own license file, plus a few sizes/weights tried and rejected
  along the way (Spleen, Tamzen, the Oldschool IBM TTFs, plain non-bold
  Terminus) — kept for reference. See `NOTICE.md` for full attribution.

An MSX ROM font (HotBit) was tried too — extracted from the project
owner's own hardware — but is deliberately **not** in this repo (the ROM
itself is copyrighted; see `NOTICE.md`) and isn't part of the font
decision above.

## Hardware

Xteink X4 — ESP32-C3, 380KB RAM (no PSRAM), 800x480 1-bit e-ink, 5-way
d-pad + power button, BLE 5.0, SD card. Same target as MicroWriter.

## Next up

- `SCREEN 4` (graphics mode): prototype direct `getFrameBuffer()` writes,
  and validate `EInkDisplay::displayWindow()` (currently marked
  experimental, unused elsewhere in MicroWriter) as the partial-refresh
  strategy for it, then bind `PSET`/`LINE`/`CIRCLE`.
- `SCREEN` from inside a program. It is the firmware's command, not the
  interpreter's, so today it only works typed at the prompt — which is why
  `examples/pacman.bas` has to ask for `SCREEN 1` in a REM. Making it
  available to programs means adding a token to the interpreter, which is a
  patch rather than an integration.
- The **phantom RIGHT presses** documented in the development log: the d-pad
  registers a RIGHT nobody pressed — which in menus reads as the selection
  sliding down, since RIGHT moves down and LEFT moves up here — worse since
  the interpreter landed, and reproducible on a second X4 and on stock
  MicroSlate but never on the readers. The leading hypothesis is the
  shared-ADC button ladder being sampled across a DFS frequency transition;
  RIGHT sits in the ladder's lowest band, so a low-biased reading lands there
  specifically. The readers never call `esp_pm_configure`, we do. The
  decisive experiment (pin 80MHz, light sleep off) is written up there and
  has not been run.
- `CONT` after a break usually fails with a syntax error. Diagnosed on the
  host harness as an upstream bug, not an integration one; documented in
  [docs/HARDWARE_TESTS.md](docs/HARDWARE_TESTS.md) with the exact mechanism.
- Eventually: a real 3-way OTA/dual-boot setup (reader + MicroWriter +
  MicroBASIC on one SD card/flash) — needs `OtaBootSwitch.cpp` generalized
  from its current hardcoded 2-partition scheme, and a 3rd app partition.
  MicroWriter's own editor partition (`app1`, `0x650000`) has a lot of
  slack (~1.8MB used of 6.25MB, since it never has to fit a full reader),
  so shrinking just that one — not the reader's `app0`, which is nearly
  full with CrossInk — is the likely path once this is worth doing for
  real.

## License

MIT — see [LICENSE](LICENSE). Third-party font attribution in
[NOTICE.md](NOTICE.md).
