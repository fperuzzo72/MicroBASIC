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

- `previews/` — 16 rendered 1-bit BMPs, one per font/size combination,
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
[Tamzen](https://github.com/sunaku/tamzen-font),
[Unscii](https://github.com/viznut/unscii), the IBM CGA/EGA/VGA faces
from the [Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/),
and (for personal reference only) the character ROM of a HotBit MSX1 —
see NOTICE.md. `Tamzen 10x20` lands closest to a classic 80x24 grid on
the X4's 800x480 panel. Font pick from these is not final — pending
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
