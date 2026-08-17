# MicroBASIC

A "new" 1980s microcomputer: the [Xteink X4](https://github.com/fperuzzo72/MicroWriter#hardware)
booting straight into a text-screen BASIC environment — line editor, `RUN`,
and an MSX-style `SCREEN` command to switch between four text densities
and a full graphics mode (`PSET`/`LINE`/`CIRCLE`) — instead of a
note-taking app.

Early planning stage. No firmware code yet. This repo currently holds the
research and decisions that'll drive the first implementation — see
[docs/DEVELOPMENT_LOG.md](docs/DEVELOPMENT_LOG.md) for the full narrative
(useful for picking this project back up on a machine that doesn't have
the conversation history that produced it).

## Plan so far

- **Base**: [MicroWriter](https://github.com/fperuzzo72/MicroWriter)'s
  editor firmware — BLE keyboard host, e-ink rendering/refresh pipeline
  (`GfxRenderer`/`EInkDisplay`, including a non-blocking `FAST_REFRESH` and
  an experimental windowed/partial refresh), SD card, webserver/OTA — with
  the text editor swapped for a BASIC line editor + interpreter. Exact
  relationship to the MicroWriter repo (fork vs. dependency) not decided
  yet.
- **Interpreter**: [My-Basic](https://github.com/paladin-t/my_basic) (MIT,
  two files, designed to be embedded/extended) as the core, with
  `SCREEN`/`PSET`/`LINE`/`CIRCLE` bound by hand to the existing
  `GfxRenderer`/`EInkDisplay` — rather than any interpreter's own built-in
  graphics backend, to avoid duplicating the e-ink-tuned refresh pipeline
  MicroWriter already has.
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

Neither is built yet; this is the spec for when `SCREEN` mode-switching
gets implemented.

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
  strategy for it.
- Implement `SCREEN 0/1/2/3` mode-switching with the centering behavior
  spec'd above.
- Decide the MicroWriter relationship (fork vs. dependency) and start the
  actual firmware tree.

## License

MIT — see [LICENSE](LICENSE). Third-party font attribution in
[NOTICE.md](NOTICE.md).
