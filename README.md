# MicroBASIC

A "new" 1980s microcomputer: the [Xteink X4](https://github.com/fperuzzo72/MicroWriter#hardware)
booting straight into a text-screen BASIC environment — line editor, `RUN`,
and an MSX-style `SCREEN` command to switch between text and a full
800x480 graphics mode (`PSET`/`LINE`/`CIRCLE`) — instead of a note-taking
app.

Early planning stage. No firmware code yet — this repo currently holds the
research that'll drive the first implementation decisions.

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
  layer once `SCREEN` graphics mode gets built.
- **Text modes**: `SCREEN 0/1/2` = small/medium/large monospace text,
  `SCREEN 3` = full-panel graphics. See `research/fonts/` for the
  candidate fonts and measured character grids.

## `research/fonts/`

- `previews/` — 9 rendered 1-bit BMPs: the round-2 candidates the project
  owner actually confirmed readable on the physical device, narrowed
  down from the full field, plus two new Terminus Bold sizes added
  specifically to compare against Tamzen Bold and regular-weight
  Terminus at the same cell size. Sizes above 24x48 px were dropped —
  confirmed too large to be worth the lost columns. **480x800
  (portrait)**, not the panel's native 800x480 — CPR-vCodex's
  `BmpViewerActivity` displays at `renderer.getScreenWidth/Height()`,
  which is orientation-aware and defaults to `Portrait` (480x800
  logical); a native-panel-resolution landscape BMP gets shrunk to fit
  instead of shown 1:1. Each image was rendered at 800x480 first, then
  rotated 90° clockwise — matching the exact transform
  `GfxRenderer::rotateCoordinates()` uses for `Portrait` in CPR-vCodex.

  Each image now carries a **character-cell ruler** for measuring
  columns/rows directly off the screen: row 1 is a column ruler (blank
  in columns 1-2, then the column number right-aligned so its last
  digit lands exactly on that column, every 5 columns starting at 5);
  every row below that starts with its own 2-digit row number (rows
  2..N — row 1 is the ruler, not counted), then the actual content
  shifted right by those same 2 columns. The font's name/size still
  prints as the first line of content, immediately under the ruler.
- `src/` — the font source files (BDF/hex/TTF) the previews were rendered
  from, plus each family's own license file. See `NOTICE.md` for full
  attribution. Carries a few sizes/weights no longer used in
  `previews/` (Spleen 8x16/32x64, Terminus 16x32, Tamzen regular, the
  Oldschool IBM TTFs) — kept for reference in case any of them come
  back into consideration.

| Family | size | cell | landscape grid | content cols (minus ruler) |
|---|---|---|---|---|
| Terminus | small | 10x20 | 80x24 | 78 |
| Terminus Bold | small | 10x20 | 80x24 | 78 |
| Tamzen Bold | small | 10x20 | 80x24 | 78 |
| Terminus | medium | 12x24 | 66x20 | 64 |
| Terminus Bold | medium | 12x24 | 66x20 | 64 |
| Terminus | large | 16x32 | 50x15 | 48 |
| Spleen | medium | 16x32 | 50x15 | 48 |
| Unscii | small | 16x32 | 50x15 | 48 |
| Unscii | medium | 24x48 | 33x10 | 31 |

Font pick is not final — the three 10x20 "small" variants (Terminus
regular/Bold, Tamzen Bold) are there specifically to be compared
side by side at identical dimensions.

## Hardware

Xteink X4 — ESP32-C3, 380KB RAM (no PSRAM), 800x480 1-bit e-ink, 5-way
d-pad + power button, BLE 5.0, SD card. Same target as MicroWriter.

## Next up

- Pick the SCREEN 0/1/2 font from the physical-device comparison.
- Prototype `SCREEN 3`: direct `getFrameBuffer()` writes plus validating
  `EInkDisplay::displayWindow()` (currently marked experimental, unused
  elsewhere in MicroWriter) as the partial-refresh strategy for graphics
  mode.

## License

MIT — see [LICENSE](LICENSE). Third-party font attribution in
[NOTICE.md](NOTICE.md).
