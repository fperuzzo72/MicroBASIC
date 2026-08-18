# MicroBASIC

A "new" 1980s microcomputer: the [Xteink X4](https://github.com/fperuzzo72/MicroWriter#hardware)
booting straight into a text-screen BASIC environment — line editor, `RUN`,
and an MSX-style `SCREEN` command to switch between four text densities
and a full graphics mode (`PSET`/`LINE`/`CIRCLE`) — instead of a
note-taking app.

Early implementation stage — `editor/` builds and flashes today (see
"Status" below). This repo holds the firmware itself plus the research
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
MicroBASIC** — a scrolling character terminal (classic numbered program
lines + a [My-Basic](https://github.com/paladin-t/my_basic) interpreter
underneath) — with the full prose note editor still available from the
Home menu (now first item **"MicroBASIC"**, then Browse Files, New
Program, Settings, Sync) for anyone who wants MicroWriter's original
note-taking instead. Physical Back always exits Screen Editor to the
menu, the fallback for when a keyboard isn't paired yet.

Working today: `LIST` (with `LIST 30` / `LIST 10-200` ranges, MORE?-
paginated), `FILES` (also paginated), `RUN`, `NEW`, `LOAD`/`SAVE
"name.bas"` (serializes the actual numbered program, not the screen
buffer), `MENU`, `SCREEN n` and `CLS` as real BASIC statements, all four
`SCREEN 0/1/2/3` fonts, and classic `GOTO`/`GOSUB <line number>` (My-Basic
itself only supports alphabetic labels — this project rewrites numbered
targets to labels at `RUN` time, transparently). `SCREEN 4` (graphics)
doesn't exist yet. Builds clean with PlatformIO
(`cd editor && pio run -e xteink_x4`) and has been flashed and tested on
the actual X4 hardware throughout.

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
- **Interpreter**: [My-Basic](https://github.com/paladin-t/my_basic) (MIT,
  two files, designed to be embedded/extended) as the core, vendored in
  `editor/lib/MyBasic/` and wired in via `mb_bridge.cpp`. My-Basic's own
  GOTO/GOSUB only understands alphabetic labels, not classic line
  numbers (verified against the real interpreter, not assumed — see the
  development log) — this project keeps its own classic numbered program
  store (`program_store.cpp`) and rewrites it to label-based source only
  at `RUN` time, so typed BASIC still reads and behaves like 1980s BASIC.
  `SCREEN`/`CLS` are already bound as real My-Basic functions;
  `PSET`/`LINE`/`CIRCLE` will follow the same pattern once `SCREEN 4`
  exists, bound by hand to the existing `GfxRenderer`/`EInkDisplay` rather
  than any interpreter's own built-in graphics backend, to avoid
  duplicating the e-ink-tuned refresh pipeline MicroWriter already has.
  [tinybasic](https://github.com/slviajero/tinybasic) and
  [PicoBB](https://github.com/Memotech-Bill/PicoBB) (BBC BASIC for
  RP2040) are architecture references, not dependencies — PicoBB's
  `framebuf.c`/`vducmd.c` split (hardware-agnostic line/circle/triangle/
  flood-fill on top of three primitives) is the model for the graphics
  layer once `SCREEN 4` gets built.
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
- On-screen status feedback beyond LOAD/SAVE's `Loaded.`/`Saved.`/errors
  — nothing yet for e.g. a program too large for the RUN/serialize
  buffers to hold.
- Eventually: a real 3-way OTA/dual-boot setup (reader + MicroWriter +
  MicroBASIC on one SD card/flash) — needs `OtaBootSwitch.cpp` generalized
  from its current hardcoded 2-partition scheme, and a 3rd app partition.
  MicroWriter's own editor partition (`app1`, `0x650000`) has a lot of
  slack (~1.6MB used of 6.25MB, since it never has to fit a full reader),
  so shrinking just that one — not the reader's `app0`, which is nearly
  full with CrossInk — is the likely path once this is worth doing for
  real.

## License

MIT — see [LICENSE](LICENSE). Third-party font attribution in
[NOTICE.md](NOTICE.md).
