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

- `previews/` — 15 rendered 1-bit BMPs, one per font/size combination,
  meant to be copied straight onto the device's SD card and viewed with
  the reader. **480x800 (portrait)**, not the panel's native 800x480 —
  CPR-vCodex's `BmpViewerActivity` displays at `renderer.getScreenWidth/
  Height()`, which is orientation-aware and defaults to `Portrait`
  (480x800 logical); a native-panel-resolution landscape BMP gets shrunk
  to fit instead of shown 1:1. Each image was rendered at 800x480 first,
  then rotated 90° clockwise — matching the exact transform
  `GfxRenderer::rotateCoordinates()` uses for `Portrait` in CPR-vCodex,
  not a guessed rotation. Each starts with the font's own name and cell
  size printed in that font, followed by the full charset and the
  standard pangram, repeated to fill the screen.
- `src/` — the font source files (BDF/hex/TTF) the previews were rendered
  from, plus each family's own license file. See `NOTICE.md` for full
  attribution.

Candidates: [Spleen](https://github.com/fcambus/spleen),
[Terminus](https://terminus-font.sourceforge.net/),
[Tamzen](https://github.com/sunaku/tamzen-font) (bold weight — the
regular weight read too thin/light on e-ink), [Unscii](https://github.com/viznut/unscii),
and the IBM CGA/EGA/VGA faces from the
[Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/).

Sizing floor: **10x20 px is the smallest readable cell** on the physical
panel (smaller was tried first and rejected — see git history). Every
`small`/`medium`/`large` triplet below now targets roughly the
80-column / 64-column / 40-column landscape grid, using each family's
native BDF size where one exists at/above that floor, otherwise a clean
integer nearest-neighbor upscale (2x/3x/4x) of the largest available
native size — never a fractional/distorting scale:

| Family | small | medium | large |
|---|---|---|---|
| Spleen | 12x24 (native, 66 cols) | 16x32 (native, 50 cols) | 32x64 (native, 25 cols) |
| Terminus | 10x20 (native, 80 cols) | 12x24 (native, 66 cols) | 16x32 (native, 50 cols) |
| Tamzen Bold | 10x20 (native, 80 cols) | 20x40 (2x, 40 cols) | 30x60 (3x, 26 cols) |
| Unscii | 16x32 (2x of 8x16, 50 cols) | 24x48 (3x, 33 cols) | 32x64 (4x, 25 cols) |
| Oldschool IBM | CGA @ ~10px (80 cols) | EGA @ ~12px (66 cols) | VGA @ ~20px (40 cols) |

The MSX ROM font preview (HotBit) was pulled from `previews/` in this
pass — only these 15 got regenerated at the new sizing. Re-add on
request once its own scaling is worked out (its native cell is a square
8x8, so the same 2:1 width:height integer-scale approach doesn't
directly apply). Font pick from all of this is not final — pending
visual comparison on the physical device.

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
