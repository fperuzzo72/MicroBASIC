# Development log

Session-continuity notes: what's been decided, what was tried and
rejected, and why — written so this project can be picked back up on a
machine that doesn't have the conversation history that produced it.
Chronological.

## The idea

A "new" 1980s microcomputer: take the [Xteink X4](https://github.com/fperuzzo72/MicroWriter#hardware)
(the e-ink writer/reader hardware [MicroWriter](https://github.com/fperuzzo72/MicroWriter)
already targets) and instead of booting into a note-taking app, boot
straight into a text-screen BASIC environment — line editor, `RUN`, and
an MSX-style `SCREEN` command to switch between text density levels and a
graphics mode. Named **MicroBASIC**.

## Base firmware: why MicroWriter

MicroWriter already solves everything *except* the BASIC part: BLE
keyboard host, e-ink rendering with a tuned refresh pipeline
(`GfxRenderer`/`EInkDisplay` — `FAST_REFRESH` with a custom LUT, and a
non-blocking refresh state machine), SD card, webserver/OTA for file
transfer. The plan is to reuse that base and swap MicroWriter's text
editor for a BASIC line editor + interpreter. Exact relationship to the
MicroWriter repo (fork vs. dependency) isn't decided yet.

### A detour: is there anything useful in MicroWriter's own history?

MicroWriter's git history shows it briefly started as a fork of
[SUMI](https://github.com/psychoplath9450/SUMI) (an EPUB reader with a
BLE-keyboard writer and a Game Boy emulator bolted on) before switching to
[CPR-vCodex](https://github.com/franssjz/cpr-vcodex) as its base. Two
things worth knowing from that investigation:

- SUMI's own e-ink rendering stack (`GfxRenderer`/`EInkDisplay`) isn't
  original to SUMI — it's ~75% inherited from
  [Papyrix](https://github.com/bigbag/papyrix-reader) →
  [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
  MicroWriter's current stack (via MicroSlate) is a sibling of that same
  lineage, already carrying the same `FAST_REFRESH`/custom-LUT
  capability — nothing needed importing from SUMI for that.
- SUMI's Game Boy emulator writes directly to the raw framebuffer
  (`getFrameBuffer()`) with a 4×4 ordered Bayer dither, bypassing the
  normal glyph/shape drawing API for per-pixel throughput — exactly the
  technique `SCREEN 4` (graphics mode) will need. MicroWriter's
  `GfxRenderer`/`EInkDisplay` already expose `getFrameBuffer()` today, so
  this capability already exists, unused. Separately, SUMI's *actual*
  e-ink-specific trick for the emulator wasn't a rendering technique at
  all — it was `gb_eink_patches.h`, a table of ROM byte-patches that
  disable flash/fade animations *inside specific emulated games*
  (Tetris's blinking "PRESS START", etc.) to avoid ghosting. Not directly
  reusable (no third-party games to patch here), but a real design lesson:
  a BASIC program that does something like a fast `CLS` loop will hit the
  same e-ink ghosting problem.
- `EInkDisplay::displayWindow(x, y, w, h, turnOffScreen)` exists today,
  marked `EXPERIMENTAL`, and doesn't appear to be called anywhere in the
  current MicroWriter codebase. This — a partial/dirty-rectangle refresh —
  is the natural candidate for `SCREEN 4`'s refresh strategy, and hasn't
  been validated on real hardware yet.

## BASIC interpreter: My-Basic + PicoBB as reference

Researched several small/embeddable BASIC interpreters:

| Candidate | License | Verdict |
|---|---|---|
| [uBASIC](https://github.com/adamdunkels/ubasic) | permissive | Too minimal (int-only, 1-letter var names) |
| [My-Basic](https://github.com/paladin-t/my_basic) | MIT | **Chosen.** 2 files, designed to be embedded/extended |
| [tinybasic](https://github.com/slviajero/tinybasic) | BSD-3-Clause | Architecture reference, not a dependency — explicitly ESP32-targeted with a per-platform runtime split, but its own e-ink support is unfinished/unmerged |
| [BBCSDL/PicoBB](https://github.com/Memotech-Bill/PicoBB) | zlib | Reference for the graphics layer (see below), not the interpreter itself — never ported to ESP32-C3 |
| [MMBasic](https://mmbasic.com/source.html) | source-available, **not redistributable without permission** | Rejected — license incompatible with an open project |

Decision: **My-Basic** as the interpreter core, with `SCREEN`/`PSET`/
`LINE`/`CIRCLE` bound by hand to MicroWriter's existing `GfxRenderer`/
`EInkDisplay` — rather than using any interpreter's own built-in graphics
backend, to avoid duplicating the e-ink-tuned refresh pipeline that
already exists. **PicoBB** (BBC BASIC for RP2040) is the model for how to
structure that graphics layer: its `framebuf.c` (`point()`, `hline()`,
`getpix()` — three hardware primitives) plus `vducmd.c` (hardware-agnostic
`line()`/`triangle()`/`ellipse()`/flood-fill built on top of just those
three) is a clean separation worth copying architecturally. Since
MicroWriter's `GfxRenderer` already has `drawPixel`/`drawLine`/`drawRect`/
`fillRect`, the pieces actually worth porting from PicoBB are what's
missing: **circle/ellipse, triangle, and flood-fill**.

## SD card layout

This repo's folder name, `MicroBASIC`, doubles as the on-device SD card
folder MicroBASIC will use for its own files — the same convention the
dual-boot reader siblings (CrossPoint/CrossInk/CPR-vCodex) already use, so
one SD card can carry MicroBASIC alongside MicroWriter and the readers
without swapping cards or file collisions.

## Font research

### Candidates and why

Wanted a genuinely monospace bitmap font (unlike MicroWriter's own
proportional NotoSans/Bookerly/OpenDyslexic fonts, which don't support a
fixed character grid at all — checked their `EpdFontData` format, glyph
advance width is per-character, not fixed). Researched:

- [Spleen](https://github.com/fcambus/spleen) — BSD 2-Clause, native
  sizes 5×8 to 32×64.
- [Terminus](https://terminus-font.sourceforge.net/) — SIL OFL 1.1,
  native sizes 6×12 to 16×32, both regular and bold.
- [Tamzen](https://github.com/sunaku/tamzen-font) — free to use/copy/
  modify, fork of Tamsyn, sizes 5×9 to 10×20, regular and bold.
- [Unscii](https://github.com/viznut/unscii) — public domain/CC0, native
  8×8 and 8×16 only.
- The IBM CGA/EGA/VGA faces from the
  [Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/) —
  CC BY-SA 4.0, hardware-authentic ROM recreations, TTF (vector, not
  bitmap) format.
- A HotBit MSX1 ROM font, extracted by the project owner from their own
  hardware (`hotbit13p.rom`, 32KB MSX1 BIOS+BASIC) — CGTABL read from
  offset 4 of the ROM (`0x1BBF` in this dump), 256 glyphs × 8 bytes each
  starting there. **Not included in this repo** — the character ROM
  itself is copyrighted (Microsoft/ASCII Corporation), unlike every other
  candidate. The raw extracted glyph table has never been committed
  anywhere, kept local-only on the project owner's machine. A preview BMP
  was rendered once for personal comparison but was dropped from
  `previews/` in a later cleanup pass along with everything else that
  didn't survive to the final decision.

### Why the previews ship as 480×800, not 800×480

First round of preview BMPs was rendered at the panel's native 800×480
and looked broken on-device: CPR-vCodex's `BmpViewerActivity` displays at
`renderer.getScreenWidth()/getScreenHeight()`, which are
orientation-aware and default to `Portrait` (480×800 logical) — a
larger-than-that landscape BMP gets shrunk to fit instead of shown 1:1.
Fix: render at 800×480 as before, then rotate 90° clockwise before
saving. The exact rotation direction wasn't guessed — read
`GfxRenderer::rotateCoordinates()` in the CPR-vCodex source directly:
`Portrait` maps logical `(x,y)` to panel `(phyX,phyY)` via `phyX=y,
phyY=panelHeight-1-x`, which is algebraically a 90°-clockwise rotation of
a native-panel-space image. Verified against the exact formula before
batch-rotating all files, not just visually eyeballed.

### The 10×20 readability floor

Initial preview batch went as small as 8×16 (Spleen/Terminus) and even
6×12 (Tamzen). Physical-device feedback: **anything smaller than 10×20px
is too small to read.** That became a hard floor, not a "small" tier —
every family's size ladder got rebuilt around it: use a native size at or
above 10×20 where the font has one, otherwise a clean *integer*
nearest-neighbor upscale (2×/3×/4× — never a fractional/distorting scale)
of the largest available native size.

### Narrowing down (round 2 → round 3)

After physical-device testing of the recalibrated sizes, feedback landed
on:

- **Terminus** (regular) at 16×32 and **Spleen** at 16×32 — both read
  well, ~48 characters/line.
- **Terminus** at 12×24 — legible, worth trying in **bold** too.
- **Terminus** at 10×20 — also worth a bold comparison.
- **Unscii** at 24×48 (32×10 characters) — "realistic, comparable to a
  Sinclair ZX81."
- **Unscii** at 16×32 (~48×15) — also good.
- **Tamzen Bold** at 10×20 — readable "forcing it a bit," wanted head-to-
  head against Terminus Bold at the same 10×20.
- Anything with a cell bigger than 24×48 — not worth it, drops too many
  columns for the size gained.

This produced the "narrowed" set (9 images) plus a request for
**Terminus Bold** at every size Terminus regular had, to compare
side-by-side at identical dimensions. Terminus Bold turned out to have a
native size at all three tiers that mattered (10×20, 12×24, 16×32 — no
scaling needed for any of them), plus a 2× scale of its own 12×24 for a
4th, larger "xlarge" (24×48) tier reaching toward ~32-column territory
(lands on 33 columns, not exactly 32 — 800px doesn't divide evenly by a
25px cell; hitting 32 exactly would mean cropping a couple of pixels of
screen edge that's under the device bezel anyway).

### Measuring aid: the character-cell ruler

To make it possible to count columns/rows directly off a photo of the
device screen, every preview image got a ruler baked in using the font's
own characters:

- **Row 1** is a column ruler: blank in columns 1-2 (to match the width
  of the row-number prefix used everywhere else), then every 5th column
  (5, 10, 15, ...) gets its number **right-aligned so the last digit
  lands exactly on the column it marks** — avoids overflow at the right
  edge and avoids collisions between adjacent numbers at this spacing.
- **Every row below that** (rows 2..N — row 1 is the ruler, not counted)
  starts with its own 2-digit row number, then the actual content
  (charset dump, then the standard pangram, repeating to fill the
  screen) shifted right by those same 2 columns.

### The Unscii resize algorithm (the deep dive)

Unscii's only native Latin sizes are 8×8 and 8×16 — both below the 10×20
floor. 16×32 (2×) and 24×48 (3×) are clean integer upscales, no issue.
10×20 (1.25×) and 12×24 (1.5×) are **not** integer ratios, and this
turned into a multi-round tuning process:

1. **Naive nearest-neighbor** (point-sample one source pixel per
   destination pixel): an isolated 1px-wide source stroke landed on
   either 1 or 2 destination pixels *depending on its exact column
   position* — same glyph feature, inconsistent width depending on
   phase. This is what made `m`/`w`/`M` look uneven and generally read as
   "not quite right," though not yet flagged as broken.

2. **Area-coverage resize** (expand the source bitmap to the exact LCM
   canvas of both grids — an exact integer block-duplication, no
   approximation — then shrink back down by exact per-block coverage
   average, threshold at 50%): fixed the phase-dependence (verified
   mathematically that the block partitioning is itself mirror-symmetric,
   so a source glyph's own asymmetry — not the algorithm — is what you
   see in the output). This made `m`/`w`/`M` noticeably better. But: some
   thin single-pixel features (`#`, `*`, `}`, the stem of `l`/`I`/`T`, the
   circumflex on `Â`/`Ê`/`Ô`) came out *too thin* — a lone 1px source
   stroke at this ratio always straddles a destination-pixel boundary, so
   a ≥50% threshold keeps only whichever side happens to get the larger
   share, and that share flips with the stroke's exact position.

   (A side investigation here: the `{`/`}` thickness mismatch the user
   first noticed turned out to be **inherited from Unscii's own source
   bitmap** — `{` and `}` are not pixel-perfect mirrors of each other in
   `unscii-16.hex` to begin with. Confirmed by mirroring `{`'s raw
   bitmap and diffing row-by-row against `}`: they differ. Not a resize
   bug.)

3. **`>0` threshold** (any coverage at all lights the destination pixel):
   fixes the thin-stroke case — a straddling stroke now always lights
   both pixels it touches — but overcorrected. Already-solid strokes
   (`I`, `T`, `|`) ballooned to 4px wide, and narrow gaps between close
   features (`W`'s middle notch, `m`'s three legs) got bridged/merged
   because even a sliver of coverage now counts.

4. **Threshold sweep** (1%, 12%, 25%, 37%, 50% coverage, visually
   compared on the worst offenders): **25%** was the best balance —
   `I`/`T`/`l` narrower than the `>0` version without going back to
   inconsistent width, `m`'s three legs distinguishable again, `W`'s
   notch visible again.

5. Even at 25%, some strokes still hit **4px wide** in places (the `|`
   pipe character rendered a solid 4px column top to bottom). Rule
   requested: cap stroke thickness at 3px, max.

6. **First attempt at capping: flat run-length cap in both directions**
   (any contiguous run of on-pixels, row-wise *and* column-wise, longer
   than 3 gets trimmed to 3, centered) — this was a real bug, not just an
   imperfect tuning: it conflated stroke *thickness* with stroke
   *length*. A tall stem is a long *vertical* run of on-pixels at roughly
   constant column — capping vertical runs at 3 chopped `I`/`T`/`|` down
   to 3px *tall* instead of 3px *wide*, destroying the glyphs.

7. **Second attempt: only cap width where the column also has a "tall"
   vertical run (≥4 rows)** — better, but still wrong: a serif bar (like
   `I`'s top/bottom crossbar) sits directly on top of the stem and shares
   its columns, so the bar's own row got flagged as "part of a tall
   stroke" too and got capped down to the stem's width, erasing the
   serif.

8. **What actually works: compare each row's run length to the glyph's
   own *typical* (median) run length across all its rows.** Only cap a
   run when it's a *small* overshoot of that typical width (within
   `typical + 1`) — i.e. clearly the stem rounding up by one pixel on
   this particular row. A row that's *dramatically* wider than typical
   (an 8px serif bar vs. a 4px stem) is left alone, because that width is
   deliberate, not a rounding artifact. This is the shipped algorithm —
   see `cap_stem_width()` in
   `research/fonts/tools/generate_screen_fonts.py`.

9. **ç/Ç cedilla**: the hook was originally fixed by shifting 3 specific
   rows of the *raw* 8×16 source bitmap left by 1px before scaling. That
   didn't survive the resize cleanly — the C body's own unshifted last
   row bled into the same *output* row as the shifted hook during the
   area-coverage resize (rows blend across the 1.25×/1.5× scale
   boundary), so the final bitmap still showed an unshifted tail sitting
   right above the shifted hook, reading as "still not moved." Fixed
   instead by shifting in the **scaled** output: render the plain `c`/`C`
   through the identical pipeline, diff it row-by-row against `ç`/`Ç` to
   find exactly where they first actually differ (not just where the
   plain letter's content happens to stop — the two aren't the same
   thing, per the bleed above), and shift only those rows left by 1px.

Reproducible end-to-end in
`research/fonts/tools/generate_screen_fonts.py` — regenerates all 8
files in `previews/` from `src/`, with the full reasoning for each
decision point in the module docstring.

## Final decisions

- **Default font: Unscii**, four sizes matching `SCREEN 0/1/2/3` (see
  README's SCREEN table). Terminus Bold as a future settings-menu
  alternative, at the same four sizes.
- **`SCREEN 0/1/2/3/4`** = 32/48/64/80 columns text / graphics — see
  README.
- **Boots into `SCREEN 1`** (48 columns) by default.
- **Default orientation: landscape, counter-clockwise** — the panel's
  native physical orientation, no rotation transform needed — chosen so
  the X4's physical side buttons land at the top of the screen.
- **Column centering**: `SCREEN 0/1/2` (32/48/64 columns) each land on
  exactly 768px of the 800px panel width (24×32 = 16×48 = 12×64 = 768) —
  16px of blank margin on each side, centered, no border drawn. `SCREEN 3`
  (80 columns × 10px) exactly fills the full 800px, no margin.
- **Row centering: none needed, ever** — 480px panel height divides
  evenly by all four cell heights (20/24/32/48), landing on exactly
  24/20/15/10 rows with zero leftover pixels in every mode.

Neither `SCREEN` mode-switching nor the graphics mode is implemented
yet — both are still just this spec.

## First test firmware: SCREEN 1 grid editor

Built to validate the SCREEN 1 spec on real hardware before writing any
interpreter code. On a branch of MicroWriter (`microbasic-screen-editor-test`,
commit `97fb59c`), not this repo yet at the time:

- Generated `unscii_16x32.h`, an `EpdFontData` C header for Unscii at the
  SCREEN 1 size, hand-emitted rather than run through MicroWriter's own
  `fontconvert.py` — that script only loads fonts via FreeType
  (`face.load_glyph(...)`), and this project's Unscii resizer produces raw
  pixel arrays, not a font file FreeType can open. `EpdGlyph`/
  `EpdFontData`'s exact field semantics (`left`/`top`/`advanceX`) were
  read directly out of `GfxRenderer.cpp`'s `renderChar()` pixel-placement
  math (`screenX = x + glyph.left + glyphX`, `screenY = y - glyph.top +
  glyphY`) rather than assumed — every glyph in this font is the *entire*
  16×32 cell with `left=0, top=0`, so `y` in `drawText()` calls is simply
  the pixel row of the cell's top edge, no baseline-offset math needed by
  callers. 191 glyphs (ASCII + the full Latin-1 Supplement block, which
  Unscii covers with zero gaps — checked before committing to a single
  contiguous interval for it). Generator:
  `research/fonts/tools/emit_epdfont_header.py`.
- Added `screen_editor.{h,cpp}`: the 48×15 grid + cursor, no
  scrolling/paging in this first pass (stops advancing at the last cell
  rather than wrapping the grid).
- `main.cpp`'s three copies of the `Orientation` → `GfxRenderer::Orientation`
  switch got collapsed into one `applyOrientationToRenderer()` helper —
  needed because SCREEN_EDITOR forces landscape-CCW on entry and restores
  the user's real setting on exit, *without* going through the persisted
  `currentOrientation` global (that variable auto-saves to NVS/SD on every
  change — routing the temporary override through it would have
  persisted "landscape-CCW" as the user's actual preference).
- Repurposed Home's "New Note" (`mainMenuSelection == 1`) to enter
  SCREEN_EDITOR instead of the prose editor's title-prompt flow. No
  BLE-keyboard-configured gate was added — none existed anywhere in
  MicroWriter to hook into (text editor entry was already unconditional),
  and physical Back already exits any screen regardless of state, so
  "just press Back if you haven't paired a keyboard yet" already worked
  for free.
- Typing reuses MicroWriter's existing US-International dead-key engine
  (`dead_keys.h`) — same one the prose editor uses — so accented input
  works in the grid editor too, decoding the dead-key engine's UTF-8
  output back to a single codepoint per cell via `utf8NextCodepoint()`.
- Builds clean (`pio run -e xteink_x4`): 1,686,640 bytes flash (was
  1,669,904 before this change — the font adds ~17KB), no new compiler
  warnings.
- Flashed to the physical X4 as a **slot-only** write to `0x650000` (the
  editor's dedicated OTA partition), *not* the merged bootloader+
  partitions+app image — the device already had a real CrossPoint-lineage
  dual-boot (reader + MicroWriter) installed, and a full `0x0` flash would
  have wiped both the reader and the NVS-stored BLE keyboard pairing.
  Slot-only leaves the reader and NVS untouched.
- Confirmed while investigating this: the dual-boot partition assignment
  is **fixed**, not symmetric — the reader always builds into `app0`
  (`0x10000`) and the editor always into `app1` (`0x650000`) (see
  `README.md`'s merge_bin example and every `build-*.yml` workflow). That
  matters for future partition planning: `app1` only ever has to fit the
  editor (1.6-ish MB used of its 6.25MB), never a full reader, so it has
  far more slack than `app0` (already 5.9-6.1MB used by CPR-vCodex/
  CrossInk) — shrinking `app1` to make room for a 3rd OTA partition is
  much safer than touching `app0`.

## Repo consolidation: MicroBASIC becomes the real repo

Decision: move the firmware into *this* repo for good, as `editor/` —
copied straight from the MicroWriter branch above (`git archive` of the
tracked files only, no `.pio` build cache), **not a fork or submodule,
and not kept in sync with MicroWriter upstream**. Rationale: MicroWriter's
own future changes aren't expected to be relevant to MicroBASIC's
direction from here; anything that does turn out useful gets ported over
deliberately later instead of tracked automatically. `editor/LICENSE`
(MIT, from MicroSlate via MicroWriter) is preserved unchanged, as its
license requires — see `NOTICE.md` for the exact source commit and full
attribution chain. Verified the copy builds identically
(`cd editor && pio run -e xteink_x4` — same 1,686,640-byte output) before
committing it here. The MicroWriter branch this was copied from has
since been discarded — this repo is the only copy now.

## Bug: typed characters landed one row below the cursor

First on-device typing test: cursor block rendered in the right cell,
cursor movement and end-of-line wraparound were both correct, but every
typed character appeared one full row *below* the cursor instead of in
it.

Read `GfxRenderer::drawText()` directly rather than re-deriving the pixel
math from scratch (the module docstring in `emit_epdfont_header.py` had
only documented `renderChar()`'s half of it):

    yPos    = y + fontData.ascender          // drawText(), BEFORE renderChar
    screenY = yPos - glyph.top + glyphY       // renderChar()

`emit_epdfont_header.py` had set every glyph's `top=0` (correct — see the
script's own reasoning for why) but the font's overall `ascender` field
to `cell_h` (32), not 0. Since `top=0` requires `ascender=0` for those two
opposing offsets to cancel out, every character was being pushed down by
a full 32px (one row) relative to where `y` actually pointed. The cursor
block is drawn with `fillRect()` directly, which never goes through
`drawText()`/`ascender` at all — staying visually correct is exactly what
made the mismatch obvious as a *relative* (cursor vs. character) offset
rather than a uniform one.

Fix: `ascender` set to `0` in the generator, header regenerated, both
flashed as a slot-only write to `0x650000` (same reasoning as before —
leaves the reader and NVS-stored BLE pairing untouched) for the retest.
Confirmed fixed on hardware: cursor and typed characters both land on the
same row now, including the inverted-cursor-cell rendering.

## Two editors, one program repository; LOAD/SAVE/MENU

Decision, after the first successful on-device test: don't make Screen
Editor *replace* the prose editor, run both as distinct tools over the
same file repository. Home now has five base items: Browse Files, Screen
Editor, **New Program** (the original "New Note" / prose `TEXT_EDITOR`,
just relabeled — still exactly the createNewFile() + title-prompt flow
it always was), Settings, Sync. The idea: the grid-faithful Screen Editor
for SCREEN-mode-accurate work, but the full prose editor (word-wrap,
typewriter/pagination modes, etc.) is sometimes just a nicer *tool* for
banging out a program's source, especially before there's a real BASIC
editing experience (line renumbering, syntax help) to make the grid
editor's raw character-grid feel worthwhile on its own.

`ui_renderer.cpp`'s `baseMenuItems[]`/`BASE_MENU_COUNT` and
`input_handler.cpp`'s `dispatchEvent()` `MAIN_MENU` case both hardcode
this list/order — no menu-entry table, so adding an item means touching
both, same constraint noted the first time SCREEN_EDITOR was added.

Also added direct-mode commands inside the Screen Editor itself — typed
as the *entire* content of a row, then Enter, instead of a normal newline
(case-insensitive; the row is cleared first, so command text never ends
up saved as if it were program content):

- `MENU` — same effect as pressing Back (restores the real orientation,
  returns to Home). Anticipating Screen Editor eventually becoming the
  system's actual boot screen (no Home menu at boot at that point) rather
  than something entered/exited through one, per the original spec — at
  that point a physical Back button alone won't be the only way out.
- `SAVE name` / `LOAD name` (quotes around the name optional) — whole
  15-row grid as plain UTF-8 text, one line per row with trailing spaces
  trimmed, under `/MicroBASIC/programs/<name>.txt` (sanitized: `/`→`_`,
  `.txt` appended if missing). `LOAD` resets the grid first; a missing
  file leaves it untouched rather than erroring destructively.

No on-screen success/failure feedback yet for LOAD/SAVE (only
`DBG_PRINTF` to serial) — the grid uses the full 480px panel height with
no spare row for a status line. Worth a real solution once this is used
for real; a one-line transient overlay is the likely shape of it.

(Superseded by the My-Basic integration below — SAVE/LOAD now persist
the actual program, not this grid buffer, and do print on-screen status.)

## My-Basic wired in for real: SCREEN_EDITOR becomes a terminal

The big one. Four things decided together, then implemented in one pass:
wire up the real interpreter, boot straight into it (Back/`MENU` as the
guaranteed way out, now that both exist), make Home's entry point for it
"MicroBASIC" instead of "Screen Editor" (first in the list — it's the
project's identity now, not just another tool), and rebuild the SCREEN
0/1/2/3 fonts for real (previously only SCREEN 1/16x32 existed as
firmware; the other three were still preview-only bitmaps).

### GOTO/GOSUB target a label, not a line number — verified, not assumed

Before writing any integration code: does My-Basic's *language* actually
support classic `10 PRINT "X"` / `GOTO 10` line-number semantics, or was
that only ever the shell's own bookkeeping? Read `shell/main.c` (My-
Basic's reference REPL) first, and it was a real surprise: `_do_line()`
there has no line-number concept *in the language* at all — anything
that isn't one of the shell's own meta-commands (HELP/CLS/NEW/RUN/BYE/
LIST/EDIT/LOAD/SAVE/KILL/DIR) just gets appended to a plain ordered
array of text lines, and `EDIT n` edits by *array position*, not by a
number embedded in the line's own text.

`my_basic.h`'s error codes confirmed the real mechanism:
`SE_RN_LABEL_DOES_NOT_EXIST`, `SE_RN_JUMP_LABEL_EXPECTED` — My-Basic's
GOTO/GOSUB targets are alphabetic labels (`mylabel:` ... `goto mylabel`),
like a structured-BASIC dialect, not classic numbered lines. Confirmed
empirically rather than trusting the grep: compiled `my_basic.c` natively
on the host and ran two test programs. `10:` (a bare numeric label)
fails to parse — `ERROR: e=48 row=1 col=2 msg=Invalid expression`.
`L10:` (letter-prefixed) works fine, including a forward `GOTO L20`
correctly skipping the line in between. Also verified, since it mattered
for the direct-mode design below: variables persist across *separate*
`mb_load_string(s, line, true)` + `mb_run(s, true)` call pairs (`A = 5`
then, as a fully separate call, `PRINT A` correctly prints `5`) — so a
REPL-style "each Enter runs its own statement" loop doesn't need to
reset anything between statements to keep that continuity.

So: this project keeps its own classic line-numbered program store
(`program_store.h/.cpp` — sorted array of `{number, text}`, insert/
replace-in-place/delete-on-blank-text, exactly the "type the number
alone to remove it" convention), and only at `RUN` time translates it
into what My-Basic actually wants: `programStoreBuildRunSource()` prefixes
every stored line with its own `LN:` label and rewrites any `GOTO n` /
`GOSUB n` found in the text (skipping string literals, so
`PRINT "GOTO 10"` is left alone) to `GOTO Ln` / `GOSUB Ln`. Classic
`GOTO 10` keeps working exactly as typed; the label indirection is
invisible.

### The terminal model

SCREEN_EDITOR stopped being a static persistent grid you free-type into
(the very first test-firmware pass) and became an actual scrolling
terminal, matching how MSX BASIC (and every other line-number BASIC)
really worked: the *screen* is temporary/transient except for what's
been explicitly committed to the program store by pressing Enter on a
numbered line — print past the bottom row and the top just scrolls off
and is gone, no scrollback. `LIST` can always reconstruct any numbered
line from `program_store` regardless of what's currently on screen; nothing
else survives a scroll.

**Logical lines can span multiple physical rows.** If a line is long
enough to wrap while typing (hits the last column, cursor auto-advances
to the next row), that's still *one* line as far as Enter/number-parsing
is concerned — `screenEditorInsertCodepoint()`'s wrap-to-next-row does
*not* reset which row counts as "the start of the current line."
Deliberately *moving* the cursor (any arrow key, Home/End, PgUp/PgDn)
does reset it — landing on a different row via explicit navigation makes
that row its own independent line reference point, matching the classic
"whatever physical row the cursor sits on when Enter is pressed gets
read as the line" screen-editor trick, generalized to also handle the
multi-row-wrap case. `screenEditorGetLogicalLineText()` concatenates
from that tracked start row through the cursor's row when Enter is
pressed; `screenEditorClearLogicalLine()` clears that same span (used for
this environment's own direct-mode commands, so their text doesn't
linger after running — numbered program lines are the opposite: they
stay visible, matching classic behavior).

Home/End operate on the current physical row (column 0 / just past the
last non-blank column) — not the logical multi-row line — matching how
a character-grid screen editor conventionally behaves. PgUp/PgDn jump to
the first/last row of the currently visible screen (no scrollback to
page through, so "first/last line" is unambiguous).

### SCREEN 0/1/2/3 fonts, for real

Only SCREEN 1 (16x32) existed as an actual `EpdFontData` header before
this pass — the other three sizes were preview-only BMPs from the font
research. `research/fonts/tools/emit_epdfont_header.py` got rewritten to
import `generate_screen_fonts.py`'s actual glyph classes directly
(`ScaledHexFont` for the clean-integer-scale 48-col/32-col sizes,
`UnsciiScreenFont` — the one with the area-coverage resize + stem-width
cap + ç/Ç fix — for the 80-col/64-col sizes) instead of re-implementing
scaling a second time, so the firmware fonts are pixel-identical to the
already-validated preview images, not a second potentially-diverging
copy. `SCREEN n` is a registered My-Basic function (`mb_register_func`,
*not* the `mb_reg_fun` macro — that one stringifies the C function's own
name as the BASIC-visible command, which isn't what you want when you
need the callable name to be exactly `SCREEN`) that calls
`screenEditorSetMode()`, which swaps the active font ID, grid cols/rows/
cell size/margin, and clears the terminal (old content wouldn't line up
under a different cell grid). `CLS` is a second registered function,
finally giving the earlier "just add CLS" question its real answer —
it's a one-line native-function binding now, not a hand-parsed special
case like `LOAD`/`SAVE`/`MENU` were in the first pass.

### LOAD/SAVE/LIST/FILES/RUN/NEW/MENU

All direct-mode commands, same "typed as the whole logical line, then
Enter" recognition as the first pass, extended:

- `LOAD "name.bas"` / `SAVE "name.bas"` (quotes optional, `.bas`
  appended if missing) now serialize `program_store` itself (`N text`
  per line) to/from `/MicroBASIC/programs/`, not the old grid-buffer
  dump — and finally print `Saved.`/`Loaded.` or an error to the
  terminal, closing the "no on-screen feedback" gap the previous pass
  left open.
- `LIST` (optionally `LIST 30` — from line 30 to the end — or
  `LIST 10-200` — that inclusive range) and `FILES` (the `.bas` files in
  `/MicroBASIC/programs/`) both print through a shared MORE?-paginated
  batch printer: fills the screen, and if more remains, prints `MORE?`
  (no trailing newline — the next keypress clears exactly that row via
  `screenEditorClearLogicalLine()`, since nothing since printing it moved
  the logical-line-start tracked above, and resumes). Nothing blocks;
  it's a tiny state machine (`PagingKind` + a resume index) checked at
  the top of every SCREEN_EDITOR key event, same non-blocking
  event-driven shape as the rest of this codebase.
- `RUN` builds the label-translated source and executes it fresh
  (`mb_reset(&bas, false, true)` — clears variables, keeps registered
  functions — classic BASIC `RUN` semantics, deliberately *not* the same
  continuity direct-mode statements get).
- `NEW` clears `program_store` (not the terminal's own visible history).
- `MENU` — unchanged from the first pass, now doubly load-bearing since
  boot goes straight here (see below).

Anything that isn't one of the above and isn't a numbered line goes
straight to My-Basic as a direct-mode statement (`mbBridgeRunDirect()`),
preserving variables across separate statements the way the native-test
above confirmed.

### Boots straight into MicroBASIC

`currentState`'s initial value in `main.cpp` is now `UIState::SCREEN_EDITOR`
instead of `MAIN_MENU`. Found a real bug while wiring this up:
`updateScreen()`'s orientation-apply block compares `currentOrientation`
against a `lastOrientation` static that defaults to `Portrait` — fine
when SCREEN_EDITOR was only ever entered *from* the menu (by then
`lastOrientation` was already synced to the user's real setting), but
booting directly into it meant that on a device with any saved
non-Portrait orientation, the very first `updateScreen()` call would see
`currentOrientation != lastOrientation` and immediately overwrite the
boot-time `LandscapeCounterClockwise` override with the user's real
saved orientation before the screen ever painted. Fixed by skipping that
block outright while `currentState == UIState::SCREEN_EDITOR` (a guard
this project actually had in an earlier draft and removed as
"provably unnecessary" — true under the old menu-entry-only assumption,
false once boot could land here directly; reinstated with the reasoning
written down this time).

### Two build problems specific to vendoring My-Basic on this toolchain

`editor/lib/MyBasic/` carries `my_basic.h` and `my_basic.c` (renamed to
`my_basic_src.inc`, see below) from
[paladin-t/my_basic](https://github.com/paladin-t/my_basic), MIT.

1. **`_lock_t` redefinition.** My-Basic's own `my_basic.c` declares
   `typedef short _lock_t;` for a small internal reference-counting type
   — invisible in `my_basic.h`'s public API, confirmed by grep before
   touching anything. ESP-IDF's newlib (`sys/lock.h`, pulled in
   transitively via `<assert.h>`) already defines `_lock_t` as a pointer
   type for real OS mutexes. Same name, unrelated purpose, hard
   redefinition error. Fixed by renaming every occurrence in the vendored
   source to `_mb_lock_t` (Python regex with a `\b` word boundary —
   macOS's default BSD `sed` does't support `\b` the way GNU sed does,
   quietly matched zero occurrences on the first attempt).
2. **Upstream's own warnings, escalated to errors by this project's
   build flags.** `missing-braces`, `unused-but-set-variable`,
   `unused-function`, integer-overflow-in-a-macro warnings, an
   `implicit-fallthrough` — all harmless, all upstream's code, none of
   them this project's to fix. Rather than patch a vendored file to
   satisfy a warning level it was never written against, the actual
   `my_basic.c` (renamed `my_basic_src.inc` so PlatformIO's library
   dependency finder doesn't also try to compile it directly and
   double-link every symbol) gets `#include`d from a 15-line
   `my_basic_impl.c` wrapper that brackets the include in
   `#pragma GCC diagnostic ignored` for exactly those warnings.
3. **RAM overflow at link time**, unrelated to My-Basic itself — this
   project's own static buffers. First-pass sizing (`MAX_PROGRAM_LINES`
   200 × `MAX_PROGRAM_LINE_LEN` 200, plus *three separate* 16KB static
   buffers for save/load/run-source) overflowed the ESP32-C3's DRAM
   segment by ~12.4KB at link time. Trimmed to 100 × 160 for the program
   store and 8KB per buffer — a real ceiling on program size now (a
   full-length program near that cap won't fit through the serialize/
   run-source buffers, failing gracefully rather than crashing), but
   comfortable for anything reasonable on a device this size. Final
   build: 188KB/327KB RAM (57%), 2.06MB/6.25MB flash (31%) — plenty of
   headroom left in both.

Flashed as a slot-only write to `0x650000` again, same reasoning as
every pass before this one (leaves the reader and NVS-stored BLE
pairing untouched).

## First real-hardware BASIC session: BLE couldn't connect, RUN froze the device, three fixes

The morning after the My-Basic integration landed, first live test on the
X4: the BLE keyboard would not pair. `Settings > Bluetooth` showed the
known keyboard, even showed it as "paired"/"active," but nothing typed
moved the cursor anywhere — not in the prose editor, not in
SCREEN_EDITOR. Chased this live over a serial connection
(`pio device monitor` doesn't work in this shell's sandbox — no
controlling tty for miniterm's `termios.tcgetattr()` — worked around by
reading the port directly with a small pyserial script instead) across
several dead ends before finding the real cause.

### False lead #1: wrong OTA partition

First hypothesis, and it did turn out to be *a* real thing (just not
*the* thing): the device booted back into the **reader** app (CrossPoint/
CPR-vCodex), not MicroBASIC. Its own log lines (`/.crosspoint/
sleep_cache/`, `[ACT] Exiting activity: Home`) gave it away — the X4 had
last been used for reading, and `esptool write_flash` to `0x650000`
doesn't touch `otadata` (the boot-partition selector), so a reset just
reboots into whichever app otadata already points at. Fixed by finding
MicroWriter's own name in the reader's app-switcher menu (registered via
`registerOtaAppName("MicroWriter")` at boot, from the very first
dual-boot pass) and switching over. Confirmed via a `[BOOT] otadata:
active slot=0 ... wrote slot=1 -> app1` log line the moment the switch
happened. Real, but didn't explain why BLE *still* didn't work once
MicroBASIC was actually running.

### False lead #2: build had no logging at all

Every `DBG_PRINTF`/`DBG_PRINTLN` in this codebase compiles to nothing
under `-DRELEASE_BUILD` (in `platformio.ini`'s `build_flags`) — including
never calling `Serial.begin()`. That's *why* the serial capture showed
nothing from our own code at first, only the ESP-IDF/NimBLE framework's
own `ESP_LOGI` lines (a completely separate logging path, unaffected by
that flag). Temporarily dropped `-DRELEASE_BUILD` for this debugging
session to get real visibility — every fix below was actually diagnosed
live against instrumented builds, not guessed at from the release build's
silence.

### The real cause: `xTaskCreate` for the BLE connect task was failing

With logging back, `connectToDevice()` was clearly being called
(`[BLE] Will connect to: ...`) but `bleConnectTask()`'s very first line —
literally its first statement — never printed. That's only possible if
`xTaskCreate()` itself never ran the task body, i.e. task creation was
failing. Added one targeted diagnostic around the actual call in
`ble_keyboard.cpp`'s `startConnectTask()`:

```
DBG_PRINTF("[BLE] Free heap=%u largest=%u before xTaskCreate\n", ...);
BaseType_t xr = xTaskCreate(...);
DBG_PRINTF("[BLE] xTaskCreate returned %d, handle=%p\n", ...);
```

Confirmed on the first try: `Free heap=25284 largest=8704 before
xTaskCreate` / `xTaskCreate returned -1, handle=0x0`. The connect task
needs a 20480-byte stack (see `ble_keyboard.cpp`'s own comment on that
number — sized for Logitech's complex GATT service tree); the largest
contiguous free heap block was 8704 bytes. Allocation failure, silent
(FreeRTOS doesn't touch the output handle on failure, so it stays
whatever it was — `nullptr` — every subsequent attempt looks identical
and retries forever, which is exactly the infinite "Auto-reconnect:
trying keyboard..." loop that had been visible all along without
explaining anything on its own).

Root cause: this session's own new static allocations (`program_store`'s
100×160 array, three separate `PROGRAM_TEXT_BUFFER_SIZE` buffers at
8192 bytes each) had shrunk the ESP32-C3's residual heap pool enough that
BLE's own initialization burst — tens of KB, largely NimBLE's host/
controller buffers — now collided with the connect task's stack
requirement. This wasn't a pre-existing bug in MicroWriter; it's this
session's own new RAM cost finally landing on the wrong side of a real
constraint. Fixed by trimming further: `PROGRAM_TEXT_BUFFER_SIZE`
8192→4096, `MAX_PROGRAM_LINES` 100→60 (`config.h` / `program_store.h`,
both comments updated in place with the reasoning) — RAM usage dropped
57.4%→51.6%, and the boot-time heap log went from "90 KiB available for
dynamic allocation" to "108 KiB." Confirmed live: `Free heap=44132
largest=27648 before xTaskCreate` → `xTaskCreate returned 1` →
`[BLE-Task] Connecting to ...` → full HID subscribe sequence →
`[BLE-Task] Keyboard ready!` — first real BLE connection to complete all
session.

### `RUN` on a program with a loop froze the entire device

Next test: `10 PRINT 5+3` / `20 GOTO 10`, then `RUN`. Nothing appeared on
screen; pressing ESC filled the screen with `8`s; eventually the whole
device stopped responding to *everything*, including the physical Back
button — required the physical reset button + power-hold to recover
(not the same as the earlier "disappeared from USB" sleep episode from
the day before; this was a real hang). Serial (once reconnected)
showed the actual cause once captured mid-hang:

```
E (73132) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (73132) task_wdt:  - IDLE (CPU 0)
E (73132) task_wdt: Tasks currently running:
E (73132) task_wdt: CPU 0: loopTask
```

repeating every 10s, forever. Root cause: `mb_run()` is a single blocking
call — for a program with a loop, it doesn't return until the program
does, which for `GOTO 10` is never. Since `mb_run()` is called from
inside `loop()` (via `mbBridgeRunProgram()`/`mbBridgeRunDirect()`),
*loopTask itself* never returns to the scheduler for as long as the
program runs, which starves FreeRTOS's idle task solid and trips the
watchdog — configured here to log, not panic-reboot, so the device just
sits there, fully unresponsive, indefinitely.

Fixed using My-Basic's `mb_debug_set_stepped_handler()` — a hook called
before *every* statement, already used internally by My-Basic's own
debugger support. Registered a handler (`mb_bridge.cpp`) that every 256
steps: calls `vTaskDelay(1)` (yields to the scheduler, feeds the
watchdog, keeps BLE/display alive even mid-loop) and drains the input
queue for a pending Escape or Ctrl+C, aborting the run (returns
`MB_FUNC_ERR`, which My-Basic's own step-dispatch already treats as an
abort signal — confirmed by reading `_execute_statement()`'s handling of
a non-`MB_FUNC_OK` result from the stepped-handler call) and printing
`?Break` if found.

Why Escape/Ctrl+C from a *BLE* keyboard can reach the queue at all while
loopTask is blocked inside `mb_run()`: BLE HID notifications are
processed on the NimBLE host's own FreeRTOS task, not loopTask —
`enqueueKeyEvent()` is interrupt-safe (`noInterrupts()`/`interrupts()`
around the ring buffer), so a keypress still lands in the queue
regardless of what loopTask is doing. The **physical** Back button can't
do the same today: it only ever becomes a queued event via
`processPhysicalButtons()`, which itself only runs from `loop()` — the
same thing that's blocked. Documented as a known gap rather than solved
tonight (`input_handler.h`'s `inputConsumeBreakPending()` comment spells
this out); a real fix would need physical-button polling to move
somewhere that isn't gated on loopTask's own progress.

### Second freeze, same shape, different cause: the display-flush wait loop

Added a second fix alongside the above, for a related but separate
problem: even with the watchdog/break fix, a running loop's `PRINT`
output was invisible on screen until the program finished or was broken
out of — because the *display refresh* is also normally driven from
`loop()`'s own `updateScreen()`/`pollRefresh()` cycle, which never runs
while `loop()` itself is stuck inside `mb_run()`. Added
`screenEditorFlushDisplay()` (declared in `screen_editor.h`, implemented
in `main.cpp` since that's where the `renderer`/`gpio` instances live),
called every ~500ms of wall-clock time (not per VM step — a tight loop
shouldn't try to repaint faster than the e-ink panel's own ~635-650ms
refresh) from the same stepped handler.

First version of this reintroduced the *exact same watchdog freeze*,
just relocated: `while (renderer.isRefreshing()) renderer.pollRefresh();`
is correct logic but a tight busy-spin with no scheduler yield inside
it — for the ~640ms an e-ink refresh takes, that alone starves the idle
task just as completely as `mb_run()` did before its own fix. Confirmed
on hardware (same watchdog spam, but this time WITH the display visibly
repainting every cycle — proof the flush itself was working, just also
re-broken the yielding). Also confirmed as a second-order effect: with
this busy-spin in place, BLE input didn't just feel sluggish, it stopped
working *entirely* mid-run — the NimBLE host task apparently couldn't get
scheduled either, so notifications genuinely weren't being delivered,
not just delayed. Fixed by adding `vTaskDelay(1)` inside the wait loop
itself (`waitForRefresh()` in `main.cpp`) — same shape as the `mb_run()`
fix, same lesson: any wait loop introduced to work around loopTask being
blocked has to yield explicitly, `pollRefresh()`/polling alone isn't
enough.

### Open at end of session: CLS/CLEAR appeared not to clear the screen

`CLEAR` typed alone does nothing to the screen — traced this to *not*
being a bug: `CLEAR` is a real My-Basic language keyword (collection
`.CLEAR()`, unrelated to our `CLS`), confirmed via `grep` for
`_COLL_ID_CLEAR` in `my_basic_src.inc` — so it was never going to reach
our code. Not registering it as an alias for CLS is deliberate.

`CLS` is a different story — genuinely still open. `screenEditorReset()`
(what `mb_cls_func` calls) is straightforward and correct on inspection
(clears the whole grid array to spaces, resets cursor/logical-line
state), and My-Basic's own name resolution is case-insensitive on both
registration and lookup (`_register_func`/`_find_func` both call
`mb_strupr()` before touching the hash table — confirmed by reading
both), so a case mismatch was ruled out too. Session ended (device went
to sleep) before getting a clear answer on what the user actually
observes — whether the screen doesn't refresh at all when CLS runs, or
refreshes but e-ink ghosting leaves old content visibly behind (a real,
separate, well-known e-ink phenomenon on a mostly-text-to-blank
transition under `FAST_REFRESH`, unrelated to any of this session's
code) — pick this back up first next session.

A related, less certain report from the same session: occasional
"phantom" space characters appearing while typing that the user was
confident they hadn't pressed. Partly explained by the user's own
admission of an accidental double space in one case; a controlled test
(a longer line of letters typed with no space bar contact at all)
showed no `0x2C` (space) keycode anywhere in the raw BLE HID report log
for that run, yet the user still reported an unexplained leading space —
so this specific report doesn't reduce to a keyboard-hardware artifact
the way the very first, less careful report might have. Not chased
further before the device slept; the dead-key engine
(`dead_keys.h`, US-International accent composition) and
`screenEditorInsertCodepoint()`'s row-wrap path were both read and look
correct on inspection, so the next session should start by reproducing
with the same careful no-space-bar-contact method and correlating
against a live raw HID log before touching any code.

## Solved (mostly): the "phantom space" mystery was a phantom RIGHT-arrow

Picked back up the next day, device still awake. Same symptoms as before
— text corrupted while typing, e.g. `RUN` landing as `ru n`, `20 GOTO 10`
landing as `2 0 goto. 10` — but this time with the user typing in tight
lockstep with a live raw-HID-report watch, which finally cracked it: the
raw BLE reports were **completely clean** every single time (confirmed
repeatedly, multiple reproductions: `R`,`U`,`N`,`Enter` — nothing else,
not even a stray modifier byte). Whatever was corrupting the text was not
coming from the keyboard at all.

The user's own observation nailed it: *"O cursor se move na tela sem eu
fazer nada"* ("the cursor moves on screen without me doing anything").
Not a phantom **character** — a phantom **cursor move**. A silent
`screenEditorMoveCursor(0, 1)` (right-arrow effect) leaves the skipped
grid cell at its default `' '` value, which reads back indistinguishably
from a real typed space once the line is stored — explaining both why it
looked like a stray space *and* why the raw BLE log was always clean:
the culprit isn't a BLE report at all.

`screenEditorMoveCursor` only has one call site gated on `HID_KEY_RIGHT`
— in `main.cpp`'s `processPhysicalButtons()`, which also reads the
device's own physical D-pad. Added a `DBG_PRINTF` right at that call
site (`[PHYSBTN] SCREEN_EDITOR nav fired: ...`) and immediately caught it
live: `key=0x4F (btnUp=0 btnDown=0 btnLeft=0 btnRight=1)` — the physical
RIGHT button reading as pressed with the user's hands nowhere near the
device.

Root cause, from reading `InputManager.cpp`/`.h`: this device's D-pad
isn't wired to individual GPIOs — it's a **shared-ADC resistor ladder**
(one analog pin carries BACK/CONFIRM/LEFT/RIGHT, distinguished by
voltage). RIGHT's calibrated real-press readings are ~3-6 (near-zero
volts) and its threshold band is "anything under 750" — the entire
low-voltage end of the range, meaning it's the button most exposed to
any noise pulling the line toward ground. `DEBOUNCE_DELAY` was a mere
5ms, so a single noisy sample sustained that long is enough to register.

Tried and explicitly **ruled out** this session:
- **More debounce.** 30ms: no change. 120ms: made it *worse* (near-
  continuous phantom presses instead of two at connect time) — this
  actively rules out "just needs more time to settle"; the interference
  has structure at these timescales that a longer window doesn't filter,
  it just integrates more of it. Reverted to the original 5ms.
- **Suppressing physical-button reads for 1.5s after BLE connect.** The
  first firings were seen right around `[BLE-Task] Keyboard ready!`,
  which looked like a smoking gun (RF/current-draw noise from the BLE
  radio during connection setup). The user confirmed the suppression
  window didn't stop it — it kept happening well into normal typing,
  long after any connection event. Also confirmed the same phantom
  jump happens on **MicroSlate itself** (the pre-MicroBASIC base
  firmware), on plain menu boot, with no BLE keyboard connected at all
  — though the user also noted MicroSlate has always run its BLE stack
  regardless, so radio activity in general isn't ruled out as *a*
  contributing factor, just "the moment a keyboard finishes connecting"
  specifically. Reverted this too.

**Conclusion: this is a pre-existing hardware/analog characteristic of
the X4's shared-ADC D-pad, not a bug introduced by MicroBASIC, and not
fixed by any debounce-timing or BLE-timing lever tried tonight.** In the
original firmware it was a minor, probably-unnoticed annoyance (an
occasional wrong menu highlight). In SCREEN_EDITOR it's much more
damaging, since it silently corrupts whatever line is being typed with
no visible error. Left the `[PHYSBTN]` diagnostic in place in
`main.cpp` (harmless, `DBG_PRINTF`-gated) since it's what cracked this —
next session should start there rather than re-deriving it. Promising
untried directions for a real fix: tightening RIGHT's ADC threshold
band to sit much closer to its actual calibrated value instead of the
entire 0-750 range (currently the most exposed possible threshold
shape), or requiring several independently-timed consecutive samples
(not just "stable for N ms" against a single noisy signal) before
accepting a transition specifically for this button.

This also almost certainly explains the earlier open "CLS doesn't clear
anything" report from the previous session: if a phantom RIGHT arrow
landed while typing `CLS`, the stored text would become e.g. `CL S` —
not a recognized command (checked via exact-string match against
`"CLS"` in `executeLogicalLine()`, and My-Basic's own case-insensitive
`CLS` registration wouldn't help since the corruption isn't a case
issue, it's an inserted space splitting the token in two) — silently
falling through to `mbBridgeRunDirect("CL S")`, which would fail to
parse as anything meaningful and do nothing visible. Worth confirming
directly next session, but no longer worth treating as a separate,
unexplained bug.

One more data point from the user, important for whoever picks this up:
the phantom presses are **not** limited to the moment a keyboard
connects (already knew that) and, more importantly, the *rate* got
noticeably worse after this session's BASIC-integration changes landed
— it was apparently a rare, easy-to-miss annoyance on plain MicroSlate/
MicroWriter before, not the frequent, typing-breaking problem it is now.
Nothing this session touched the ADC/InputManager code path directly
before tonight's revert, so the plausible link is indirect: added heap/
DRAM pressure (this session's My-Basic + program_store allocations,
partially clawed back but still a net addition), and/or altered timing
around `esp_pm_configure`'s CPU-frequency scaling (80/10MHz) and light
sleep, could be changing how often loop() iterations land during
whatever noise event causes this, or subtly affecting the analog
frontend during frequency transitions. Worth testing directly: does the
phantom-press rate drop if `mbBridgeSetup()`/My-Basic's heap footprint
is temporarily removed on an otherwise-identical build?

## Interim fix that actually unblocked typing: disable physical RIGHT in SCREEN_EDITOR

The phantom-button investigation above was written up as "needs a fresh
session" — then the user came back mid-session unable to type a program
at all, cursor jumping mid-line on every attempt. That's not a
"revisit later" severity, so shipped a narrow, honest workaround instead
of a real fix: physical RIGHT no longer does anything inside
SCREEN_EDITOR specifically (`main.cpp`'s `processPhysicalButtons()`,
scoped to that one `case` only — confirmed by grep that it's the only
`UIState::SCREEN_EDITOR` arm in that switch, so MAIN_MENU/SETTINGS/
FILE_BROWSER/etc. still navigate normally with the physical D-pad).
A BLE keyboard's own Right arrow is unaffected — it arrives via HID
report parsing, not this analog GPIO read, so cursor movement while
typing still works fine from the keyboard. **Confirmed by the user: this
completely fixed typing.** Root cause (the ADC noise itself) is still
open; this just removes the one button whose threshold band happens to
sit in the noise-exposed danger zone from the typing path entirely.

## RELEASE_BUILD toggled off and back on twice more, chasing two different things

Went back into a debug build (temporarily un-defining `-DRELEASE_BUILD`
again) twice more this session:

1. To test whether the sheer volume of `DBG_PRINTF` traffic itself
   (every keystroke, every BLE report, every display-refresh step) was
   part of what made the phantom-button rate worse -- plausible since
   heavy `Serial`/USB-CDC writes can add real latency if nothing's
   draining the far end fast enough. Reflashed a clean `-DRELEASE_BUILD`
   production build and had the user retest. Typing corruption was
   already fixed by the RIGHT-button change above by this point, so this
   didn't get a clean isolated answer either way -- worth remembering
   this variable exists if the phantom-button rate is revisited.
2. To instrument `screenEditorFlushDisplay()` (in `main.cpp`) with
   millis()-based timing around the wait/draw/wait sequence, chasing a
   different report: a `PRINT`-in-a-loop program felt like it only
   updated the screen "every 3 seconds or more" -- too slow for
   anything resembling a game -- and Ctrl+C/Escape felt like they
   weren't working.

That second one turned out not to be a bug so much as an expectations
mismatch, confirmed by a real photo of the panel mid-run: the screen
showed `Teste` printed edge-to-edge, wrapped across multiple rows,
dozens of repetitions deep, all in one frame -- because **My-Basic runs
the interpreted loop far faster than the e-ink panel can possibly
redraw** (~640ms per `FAST_REFRESH`, throttled further to at most once
per 500ms of wall-clock time by this session's own flush hook). Ctrl+C
*was* taking effect essentially immediately at the interpreter level;
what looked like "not working" was the display simply still catching up
to output the program had already generated before the break landed --
`?Break` was sitting in the grid the whole time, just behind everything
printed in the gap. Confirmed directly with the user, who was satisfied
this explains it and asked to leave it as-is for now rather than chase
either a faster refresh path or an interpreter-side rate limit tonight.

Two smaller things surfaced by that same photo, neither chased further
tonight, both worth a look next time:
- A spurious `?Invalid expression Ln 0` printed immediately before the
  `?Break` line -- looks like returning `MB_FUNC_ERR` from the stepped
  handler markets a mid-statement parse into a bad state before My-Basic
  reaches the code path that would normally print a clean `?Break`
  alone. Cosmetic (both messages communicate "stopped"), but worth
  understanding.
- Visible fading/ghosting creeping in from the top of the screen down
  after enough consecutive `FAST_REFRESH` calls in a row with no
  intervening `FULL_REFRESH` -- a well-known, expected e-ink phenomenon
  when a panel never gets the periodic full clear/repaint cycle it needs
  to reset accumulated pixel drift. `drawScreenEditor()`
  (`ui_renderer.cpp`) unconditionally uses `FAST_REFRESH` today, no
  refresh-count-based full-refresh fallback exists yet. Explicitly asked
  by the user to leave this alone for tonight rather than fix it live.

Ended the session back on a clean `-DRELEASE_BUILD` production build,
slot-flashed to `0x650000` as always. The `[PHYSBTN]` and `[FLUSH]`
`DBG_PRINTF` diagnostics added while chasing these are left in the
source (both compile to nothing under `RELEASE_BUILD`, zero runtime
cost in the shipped build) -- flip the flag back off to pick either
investigation back up without re-deriving where to even look.

### Next session, phantom-RIGHT: two concrete directions from the user

1. **Isolate device vs. design.** Re-enable physical RIGHT in
   SCREEN_EDITOR (revert the `main.cpp` change above) and test on a
   *different* physical X4 unit. Everything gathered tonight was on one
   specific device -- worth ruling out that this is that unit's own
   button/contact/ADC-channel wearing out or being marginal, rather than
   a property of the shared-ADC ladder design in general. If a second
   unit doesn't reproduce it at anywhere near the same rate, the fix
   changes completely (repair/replace that button vs. a firmware-level
   noise-rejection fix).
2. **Check against upstream MicroSlate for a regression.** The user's
   recollection is that this phantom-press behavior goes back to
   original MicroSlate, specifically around when this project's US-
   International dead-key work was first added -- i.e. it may date to
   *our own* early changes to the physical-key-reading path, not to
   MicroSlate itself. Worth a direct diff: compare this repo's
   `editor/lib/InputManager/` and the physical-button-reading parts of
   `editor/src/main.cpp` (`processPhysicalButtons()`) against
   [Josh-writes/microslate-firmware](https://github.com/Josh-writes/microslate-firmware)
   (the original upstream MicroSlate MicroWriter's own editor is
   imported from -- see `NOTICE.md`) to see whether the ADC-ladder
   reading logic was ever touched while wiring up dead-key/US-
   International support, and if so, whether that change is what
   introduced or worsened the noise sensitivity.

### Direction 2, done: found the exact matching historical bug — already fixed here

Checked direction 2 above against a local checkout of the user's own
prior project, `fperuzzo72/microslate-firmware-US-International`
(`/Users/fperuzzo/github/microslate-firmware-US-International` on the
dev machine) — this is almost certainly the actual project the user
remembered, not upstream Josh-writes/microslate-firmware directly (no
local clone of that one was needed; the US-International repo's own
history already had the answer).

Found the exact match: commit `1d186ae` ("Fix InputManager button ADC
reads for dual-framework build", 2026-07-10, about two weeks after
`9632066` "Create dead_keys.h") documents -- in detail, in its own
commit message -- **this precise symptom**: *"visto na prática como um
'Right' preso (o cursor avança sozinho e sem parar no editor ...) e
como Back/Confirm/Left nunca registrando enquanto a leitura da escada
fica presa na faixa do Right"* ("seen in practice as a stuck 'Right'
(the cursor advances on its own and without stopping in the editor...)
and as Back/Confirm/Left never registering while the ladder reading
gets stuck in the Right range"). Root cause identified there: Arduino's
`analogRead()` triggers a GPIO reconfiguration on *every call* under a
dual `framework = arduino, espidf` build (exactly this project's
framework setting) -- frequent enough, called every `loop()` iteration
for button polling, to destabilize the shared-ADC ladder reading badly
enough to misclassify, landing disproportionately on RIGHT specifically
because all buttons share one ADC pin and only one classification can
win per read.

The fix there: replace `analogRead()`/`pinMode()` with direct ESP-IDF
calls (`adc1_get_raw()`, `adc1_config_channel_atten()`), matching what
the *original* MicroSlate `InputManager` did before a later migration
to a different SDK's own `InputManager` (which had regressed back to
plain `analogRead()`) reintroduced the bug there.

**Checked whether this project has the same regression: it does not.**
Grepped the whole `editor/` tree for `analogRead` -- zero real call
sites, only a comment referencing it. Both `InputManager.cpp` and
`BatteryMonitor.cpp` (the only two files touching the shared ADC1 unit)
already use the direct ESP-IDF API throughout
(`adc1_get_raw`/`adc1_config_channel_atten`), and `InputManager.cpp`'s
own `begin()` carries the identical comment/reasoning as the historical
fix almost word for word -- this project's `InputManager` was copied
from a MicroSlate lineage that already had this particular fix baked
in, not from the regressed one. Also checked whether `BatteryMonitor`
(sharing the same ADC1 peripheral, different channel) could be
interleaving with button reads and destabilizing them some other way --
`drawScreenEditor()` (`ui_renderer.cpp`) never calls
`getBatteryPercentage()`, so no battery ADC read happens at all while
SCREEN_EDITOR is what's on screen, ruling that out too for the case
that actually matters (typing).

**So: not a repeat of this specific, previously-solved bug.** This
project inherited the correct fix already. Whatever's still happening
tonight is either genuine RF/electrical noise (as suspected before) or
a hardware/contact characteristic of this specific unit -- direction 1
above (test on a second physical X4) is the more promising next lever,
not more firmware archaeology on this particular historical thread.

### One more round: user suspected the "-usi" fix itself never actually worked

The user pushed back on the framing above -- their memory is that the
US-International fixes didn't actually resolve this on their end, and
that a *second physical device* they have, running an *earlier* build,
works fine specifically because it predates that whole effort (not
because it's different hardware). Worth checking directly rather than
taking the commit message's word for it, so went looking for exactly
which "earlier" version that might be.

Found real version tags in the `microslate-firmware-US-International`
history: `v1.0.0-usi` and `v1.0.1-usi` (both 2026-06-25, the same day
`dead_keys.h` was created) and `v2.0.3-usi` (2026-06-28). Cross-checked
against `git log --all | grep freeink`: the FreeInk SDK migration (and
therefore the `analogRead()` regression it introduced, and the same-day
`1d186ae` fix for it) all happened on **2026-07-10** -- two weeks *after*
every one of those `-usi` tags. So if the "earlier, working" device is
running any of `v1.0.0-usi`/`v1.0.1-usi`/`v2.0.3-usi`, it was never on
the FreeInk SDK InputManager at all; it's running whatever InputManager
came before that migration, straight from the MicroSlate lineage.

Diffed that lineage directly: pulled `lib/InputManager/src/InputManager.cpp`
and `include/InputManager.h` as they existed at `v2.0.3-usi` (the most
recent USI tag before the FreeInk SDK switch) and diffed both against
this project's current copy. **The `.cpp` is byte-for-byte identical.**
The `.h` differs by exactly one thing: the comment this session added
above `DEBOUNCE_DELAY` while documenting the investigation -- the actual
value (`5`) and every other line are identical too. Also diffed the
`esp_pm_configure`/`setCpuFrequencyMhz` block in `main.cpp` between the
two -- also identical, including the comment.

**This is about as clean a negative result as this kind of check gets:**
the button-reading code and the CPU/power-management configuration are
provably unchanged between the version the user remembers working and
this project's current state. Whatever's different isn't in either of
those files. Left standing as the most likely explanation: not a code
regression at all, but the *system-wide* change in resource pressure
and activity level from everything this session's BASIC integration
added (more static RAM eating into the heap headroom BLE and other
subsystems draw from, more BLE traffic/activity in general with the
interpreter wired in) -- consistent with the user's own earlier
observation that the phantom-press rate specifically got worse after
that integration landed, not after any button-code change. Testing on
the user's second physical device (planned, not yet done) is still the
cleanest way to separate "this exact unit" from "this exact firmware
under more load" as the real variable.

## Upstream MicroWriter fixes not yet ported here (2026-08-18)

MicroBASIC's `editor/` forked from MicroWriter's `editor/` at some point
(exact base commit not tracked — see "Exact relationship to the
MicroWriter repo (fork vs. dependency) isn't decided yet" near the top of
this file). Since then, a MicroWriter session (`~/github/MicroWriter`)
made three rounds of fixes that this repo hasn't picked up. Confirmed by
direct grep against this repo's current `editor/src/` — none of these are
here yet. Not blind copy-paste candidates: this repo's `main.cpp`,
`ui_renderer.cpp` etc. have diverged substantially (BASIC integration,
the phantom-button investigation above), so each needs porting by hand
against MicroWriter's current source, not applied as a patch.

**1. "MicroSlate" → "MicroWriter" branding.** MicroWriter's product name
changed from MicroSlate to MicroWriter; MicroBASIC still has the old
name in the same spots MicroWriter did before its own fix:
- `ui_renderer.cpp:221` — `drawCenteredText(FONT_BODY, 30, "MicroSlate", ...)`,
  the Home-screen header text (MicroWriter changed this to `"MicroWriter"`
  — obviously don't just copy that string here verbatim, since this repo's
  Home screen presumably wants "MicroBASIC" or similar, not either of
  those; just flagging the stale string, not prescribing the replacement).
- `ble_keyboard.cpp:585` — `NimBLEDevice::init("MicroSlate")`, the
  BLE-advertised device name shown when pairing a keyboard.
- `main.cpp:712-713` — sleep-screen title string.
- `main.cpp:244,323` — `"MicroSlate starting..."`/`"MicroSlate ready."`
  debug logs.
(The attribution comment at `main.cpp:315-316`, explaining the codebase
is credited as MicroSlate in `NOTICE.md` while the product name differs,
is fine to keep as-is — MicroWriter kept the equivalent comment too.)

**2. `/microslate` → `/microwriter` SD folder migration.** MicroWriter's
`sd_backup.h` gained an `ensureSettingsDir()` helper: checks for the new
folder name first, falls back to renaming a pre-existing old-name folder
in place (preserving whatever was backed up there) instead of starting
fresh, only creates new if neither exists. This repo's `sd_backup.h`
doesn't have it — still has three separate hand-rolled
`if (!SdMan.exists("/microslate")) SdMan.mkdir("/microslate")` call
sites (`main.cpp:840`, `ble_keyboard.cpp:552`, `wifi_sync.cpp:145`) plus
the hardcoded `/microslate/...` paths at `main.cpp:272,841`,
`ble_keyboard.cpp:532`, `wifi_sync.cpp:126`. Whatever folder name
MicroBASIC settles on (may not even want "microwriter" — worth deciding
deliberately rather than copying MicroWriter's choice by default), the
*migration pattern* (rename-in-place fallback, not a blind rename) is
the part worth porting, so an existing install's saved settings don't
get silently orphaned by a folder rename.

**3. OTA dual-boot switch rollback bug — this one's a real, confirmed bug,
not just drift.** `main.cpp`'s `switchToOtaApp()` here uses the exact same
`ota_boot::switchTo(target)` call MicroWriter's did before today. Root
cause (fully diagnosed this session, see MicroWriter's own
`docs/DEVELOPMENT_LOG.md` section "The *real* sleep/wake culprit: ESP-IDF
app rollback, not otadata resets" for the full writeup): `switchTo()`
always writes the newly-selected OTA slot with rollback state `NEW`
(correct for a reader's own genuine firmware self-update, wrong for a
plain dual-boot switch to an already-working sibling slot). On hardware
where the physically-flashed bootloader has ESP-IDF's app-rollback
feature enabled, switching into MicroBASIC and then letting the device
sleep before anything confirms the slot valid gets it silently rolled
back to the sibling (reader) slot on the next wake — indistinguishable
from "sleep/wake always dumps you back into the reader even though
MicroBASIC was active." Confirmed hands-on: this is exactly the bug a
physical test device running MicroBASIC in this slot was hitting.

Fix (verified working, all four MicroWriter reader targets rebuilt and
one device retested after applying it): added a `confirmLastOtaSwitch()`
helper right next to `switchToOtaApp()` that re-reads the otadata entry
`switchTo()` just wrote (the one with the higher `ota_seq`) and flips
just its `ota_state` field from `NEW` to `VALID` (`2`) — a second
erase+rewrite of that one 4KB sector, since flash bits can only be
cleared without an erase and `NEW`(`0`)→`VALID`(`2`) needs to *set* a
bit. Deliberately does *not* touch `ota_boot::switchTo()` itself (that's
upstream reader code, reused as-is, and its `NEW`-state behavior is
correct for the reader's own genuine self-update path, which calls the
same function). See MicroWriter's `editor/src/main.cpp` (search
`confirmLastOtaSwitch`) for the exact, copy-portable implementation —
it's self-contained (just `esp_partition.h` + the `ota_boot::SelectEntry`
struct already visible via `OtaBootSwitch.h`), no other MicroWriter-only
dependencies, so this one *can* be ported close to verbatim.

## Second physical device: full backup, and a genuinely new phantom-RIGHT clue

Connected a *second* physical X4 the next morning specifically to test
whether the phantom-RIGHT issue is this-unit-specific or general. Before
touching anything on it: a full 16MB `esptool read_flash` to
`~/Downloads/X4_backups/X4_device2_fullflash_<timestamp>.bin`, verified
by checking the ESP32 image magic byte (`0xE9`) at offset 0 (bootloader),
`0x10000` (app0/CrossPoint), and `0x650000` (app1/MicroSlate) — all
present, full 16,777,216-byte file. This single dump is sufficient to
restore the device to its exact prior state, or extract either app image
individually later (their offsets are fixed by `partitions.csv`, no
separate per-partition dump needed).

This second device is running CrossPoint + an *older* MicroSlate build —
already past the original US-International dead-key work, but before
this session's own visual fixes to it (the menu selection bar eating
into the row below, cosmetic). Confirmed the phantom-RIGHT bug reproduces
on it too, immediately, on first entry into the Home menu after
switching from CrossPoint — with buttons "inverted" on this unit
specifically (RIGHT moves the menu selection *up*, not down; not yet
understood, possibly board-revision or wiring-specific, separate from
the phantom-press question itself).

**New, sharper clue from this device: the bug reproduces on a cold OTA
switch (CrossPoint → MicroSlate) but *not* on sleep/wake.** Put the
device to sleep while in MicroSlate, woke it — resumed correctly in
MicroSlate, no phantom press, not at the menu and not later in the
editor either. Both paths are full CPU resets that re-run the bootloader
and re-execute `InputManager::begin()`'s ADC configuration from
scratch, so *why* one shows the bug and the other doesn't is a real,
narrow question — not yet answered, but it rules out "just needs to run
for a few seconds to settle" (sleep/wake is instant-on and clean) and
points at something specific to the OTA-switch/warm-`esp_restart()`
path instead of general ADC noise. Worth checking next: whether a soft
`esp_restart()` leaves analog/RTC peripheral state configured by the
*previous* app (CrossPoint) in a way a true power-on-equivalent reset
(which is what deep-sleep wake amounts to) wouldn't -- i.e. whether
`InputManager::begin()`'s `adc1_config_channel_atten()` calls are
sufficient on their own to fully reclaim the channel after CrossPoint's
own ADC/GPIO usage, or need an explicit reset of the ADC unit first.

**Tested pure upstream too.** Cloned `Josh-writes/microslate-firmware`
fresh (no local checkout existed yet), built its `main` HEAD (two commits
ahead of the `v2.0.3` tag, both doc-only deletions -- functionally
identical to the tag), and slot-flashed it to this device's `app1`
(`0x650000`), CrossPoint untouched at `app0`. Same partition table
(`partitions.csv` is byte-identical in intent: `app0`/`app1` at the same
offsets/sizes) so this dropped in cleanly. Confirms the phantom-RIGHT
question can be asked against the *actual* original codebase, not a
fork's approximation of it — result of that specific test not in yet
(device still mid-comparison as of this entry).

## Ported MicroWriter's `confirmLastOtaSwitch()` fix

Implemented the port flagged as pending above. `main.cpp`'s
`switchToOtaApp()` now calls `confirmLastOtaSwitch()` right after a
successful `ota_boot::switchTo()`, before `esp_restart()` — copied close
to verbatim from MicroWriter's `27b2f65`, using the same
`ota_boot::SelectEntry`/`computeSeqCrc()` already available here via the
identical `OtaBootSwitch.h`. Builds clean. Not yet flashed/retested on
hardware — the user wants to test separately whether this changes the
phantom-RIGHT-on-cold-switch behavior from the section above, so this is
landed as a code change now, verification is a follow-up.

## Branding and SD-folder cleanup: MicroBASIC never got MicroWriter's own rename pass

MicroWriter fixed its last "MicroSlate" UI-string leaks and moved its SD
settings folder from `/microslate` to `/microwriter` in `c0d6977` — but
that commit landed *after* MicroBASIC had already forked off, so none of
it carried over here. Applied the equivalent here, adapted for
MicroBASIC's own identity (not just re-copying MicroWriter's strings):

- **UI strings**: Home screen title, sleep screen title, the two
  `DBG_PRINTLN` boot messages, and the BLE-advertised device name
  (`NimBLEDevice::init(...)`, what a keyboard shows while pairing) all
  now say "MicroBASIC" instead of "MicroSlate". The OTA-registered
  sibling identity (`registerOtaAppName(...)`, what a reader's own
  switcher menu shows for this slot) changed from `"MicroWriter"` to
  `"MicroBASIC"` too — this project's own product identity, not
  MicroWriter's. mDNS hostname: `microwriter.local` → `microbasic.local`.
- **SD settings folder**: `/microslate` → **`/MicroBASIC`**, deliberately
  *not* `/microwriter` — the user wants MicroWriter and MicroBASIC able
  to coexist on the same SD card without their settings colliding, even
  though MicroBASIC's `editor/` is a superset of MicroWriter's and
  running both would be redundant in practice. `/MicroBASIC` already
  existed as a directory for a different reason (`program_store.h`'s
  `/MicroBASIC/programs/`), which broke MicroWriter's own migration
  pattern: a straight "does the new folder already exist? then skip
  migration" check would silently skip migrating old `/microslate`
  settings the first time anyone had already saved a BASIC program.
  `sd_backup.h`'s `ensureSettingsDir()` migrates the three known settings
  files (`ble_kb.json`, `wifi.json`, `ui_prefs.json`) individually
  instead of renaming the whole directory, leaving `programs/` (and
  anything else already under `/MicroBASIC`) untouched. Called once at
  boot (`main.cpp`, right after `fileManagerSetup()`, before anything
  reads/writes settings) and reused at each of the three write sites
  instead of each doing its own ad-hoc `exists`+`mkdir`.

Left alone, matching MicroWriter's own precedent: the `NOTICE.md`
attribution, `dead_keys.h`'s credit-to-MicroSlate comment, and the
historical/dev-log-style comments elsewhere in this file and in code
that reference "MicroSlate" or "MicroWriter" as the base codebase's own
name — those describe lineage, not user-facing strings, and stay as-is.

Builds clean. Not yet flashed to either physical device — landed as a
code change alongside the OTA-switch fix above, both pending the user's
next hardware test pass.

## Confirmed on hardware: confirmLastOtaSwitch() fixes the phantom-RIGHT bug too

Flashed the build above (OTA-switch fix + rebranding) to the second
physical device. Result, confirmed by the user directly: **the phantom-
RIGHT bug is gone** — both the cold CrossPoint→MicroBASIC switch (which
reliably reproduced it every time before) and sleep/wake now behave
correctly, no phantom press either way.

This is a genuinely useful, if not fully theorized, result: the leading
hypothesis remains that leaving the newly-switched-to slot's otadata
state at `NEW` (pending-verify) rather than `VALID` was interacting with
something in the boot/peripheral-init path in a way that specifically
destabilized the shared-ADC button ladder — plausible given the bug only
ever reproduced on a *cold OTA switch into an unconfirmed slot*, never on
a clean sleep/wake cycle (both are full resets, but only one of them
involves an unconfirmed slot). Exactly *what* about an unconfirmed slot
state touches ADC/GPIO behavior isn't nailed down mechanistically, but
the fix is real and reproducible, so this is being taken as resolved for
this device rather than chased further into ESP-IDF bootloader internals
tonight. Whether this also explains device 1 (this session's original
test unit, back home) is still open — same fix should be tested there
next.

## MicroBASIC gets its own dual-boot patch set for CPR-vCodex

Asked to also reinstall an updated CPR-vCodex (the reader) alongside the
new MicroBASIC build, to see if that resolved a separate, smaller
annoyance: the reader's own Home-menu shortcut for switching to the
editor still read "MicroWriter", not "MicroBASIC".

First attempt: cloned `franssjz/cpr-vcodex` fresh (no local checkout
existed), built its `1.5.0.9-cpr-vcodex` tag plain (`pio run -e
gh_release`, after `git submodule update --init --recursive` for
`open-x4-sdk`), slot-flashed to `app0` (`0x10000`) alongside the new
MicroBASIC build on `app1`. **Didn't fix it** — the shortcut was gone
entirely, because a stock CPR-vCodex checkout has no dual-boot switching
feature at all. Searched the checked-out source directly for anything
resembling `registerOtaAppName`/`ota_names`/`MicroWriter` and found
nothing, which was the right sign one step too early: the feature isn't
missing a name, it's not there.

Root cause, from re-reading MicroWriter's own `patches/cpr-vcodex/`
(the actual source of the dual-boot Home-menu shortcut): the whole
feature is grafted onto a plain CPR-vCodex checkout at build time by
eight patch scripts, not present upstream at all — explaining why the
first build's source search came up empty. More importantly, patch 7
(`07_patch_i18n_strings.py`) revealed *why* our own `registerOtaAppName`
dynamic-NVS mechanism was never going to change the displayed text:
the shortcut's label comes from a **compile-time i18n YAML string**
(`STR_MICROSLATE: "MicroWriter"`) baked into CPR-vCodex's own
translation file by the patch, completely independent of what any
sibling app registers at runtime in `ota_names` NVS. That NVS mechanism
exists in these patches too (`OtaApps.h`'s `registerOtaAppName`/
`detectOtaApps`), but it's used the other way around — for the *editor*
side to dynamically discover and display the *reader's* name, not for
the reader to discover the editor's. A static label was never going to
respond to a runtime NVS write no matter how many times MicroBASIC's own
`setup()` called `registerOtaAppName("MicroBASIC")`.

Fix: copied MicroWriter's whole `patches/cpr-vcodex/` set into this repo
as `patches/cpr-vcodex/` (own README explaining the relationship), with
exactly one line changed — patch 7's string value,
`STR_MICROSLATE: "MicroWriter"` → `STR_MICROSLATE: "MicroBASIC"` (and
the app-description string reworded to match). Every internal identifier
(`ShortcutId::MicroSlate`, `microslateShortcut`, the `STR_MICROSLATE` key
name itself, `switchToFirstOtaApp`, ...) deliberately left exactly as in
MicroWriter's set — none of those are user-visible, and keeping them
identical means future patch updates from MicroWriter's own set stay
easy to diff/port. Re-checked out the `1.5.0.9-cpr-vcodex` tag fresh,
applied all eight scripts (`for f in patches/cpr-vcodex/*.py; do python3
"$f"; done` from the `MicroBASIC` repo root, matching each patch's own
`cpr-vcodex/...` relative paths), built clean (`gh_release` env, 92.8%
flash — CrossPoint-lineage readers are consistently this close to their
partition's ceiling), slot-flashed to `app0`.

**Confirmed by the user: the Home-menu shortcut now reads "MicroBASIC"
correctly.** Both fixes (OTA-switch confirmation + the reader's own
dual-boot patch set) are now verified working together on this device.

## Interpreter review: My-Basic is the wrong foundation, and the decision to swap

Stepped back to review the interpreter choice before building more on top of
it. Findings, from reading My-Basic's own source rather than its docs:

- `LEFT`/`MID`/`RIGHT`/`STR`/`CHR` are registered **without** the `$`
  (`my_basic_src.inc`'s `_std_libs` table), so classic `LEFT$(A$,3)` fails.
  One genuine bright spot: `A$` *as a variable* works — `$` is
  `_STRING_POSTFIX_CHAR`, a valid identifier char that also types the
  variable as a string.
- Missing entirely: `DATA`/`READ`/`RESTORE`, `ON x GOTO`, `TAB()`/`SPC()`,
  `PRINT`'s `,`/`;` zone semantics, `STOP`/`CONT`, `IF x THEN <line>`.
- Errors report the row of the *rewritten* source, not the line the user
  typed — already visible on hardware as `?Invalid expression Ln 0`.
- AST with many small allocations, which is what starved the BLE stack.

Each is individually patchable by more text rewriting on top of the
`L%d:`-label hack already in `program_store.cpp`, but they compose badly and
the error-line problem is structural, not cosmetic. My-Basic is a structured
scripting language wearing BASIC syntax; this project wants a line-numbered
BASIC.

Measured the cost of switching now: **~220 lines** depend on My-Basic
(`mb_bridge.cpp` 153 + `rewriteGotoGosub` 70). The screen editor, terminal
model, program store, LIST/FILES/SAVE/LOAD and the SCREEN modes are all ours
and interpreter-agnostic. This is the cheapest this decision will ever be.

**Decision: adopt [slviajero/tinybasic](https://github.com/slviajero/tinybasic)**
(Stefan Lenz's IoT BASIC). Native line numbers, tokenised program in one
fixed memory block, `A$`/`LEFT$`/arrays/`DATA`-`READ`/`ON..GOTO`/`DEF FN`,
MS-BASIC compatibility modes (MSX BASIC *is* Microsoft BASIC), already runs
on ESP32, and a clean interpreter/runtime split we can hook to our screen
editor. Its REPL rule is literally the one we hand-built:

```c
if (token == NUMBER) { storeline(); }  /* number -> program memory */
else                 { statement();  }  /* anything else -> direct mode */
```

It also ships `HASGRAPH` (line/circle/rect/fill/colour), which lines up with
the planned ports to Paper S3, LilyGO T5S3 and a colour CYD.

**Licence:** ambiguous upstream — the root `LICENSE` (2025) is BSD-3-Clause
but every source header still carries a GPL v3 notice. Deliberately *not*
resolved, because it doesn't need to be: GPL obligations trigger on
distribution, and this is a personal hobby build. To keep it that way the
source is **not vendored**. It gets fetched and adapted at build time by the
same `patches/` mechanism already used for the readers, so this repo stays
free of third-party licence entanglement and can remain public. A full-history
mirror lives at `../_backups/stefan-tinybasic.bundle` (see its README) as
insurance against upstream disappearing.

## Two interpreter-independent fixes done first

Both of these were worth doing regardless of which interpreter wins, so they
landed before the swap starts.

### 1. Wrapped logical lines (a real regression against MSX)

`screenEditorMoveCursor()` and friends each did
`logicalLineStartRow = cursorRow`, so navigating onto the *second* row of a
line that had wrapped past the right margin made Enter read only that tail.
That breaks the single most important trick in the MSX editor: `LIST`, cursor
up onto a listed line, edit it in place, Enter to re-store it — which fails
exactly when the line is long, i.e. when you most want it.

Replaced the tracked `logicalLineStartRow` variable with a per-row
`rowIsContinuation[]` flag, set by the two places that can actually wrap
(typing past the last column, terminal output doing the same) and cleared
wherever a genuinely new line starts. The logical line's start *and end* are
now derived by walking that chain in both directions from wherever the cursor
is. This is simultaneously simpler (navigation functions no longer maintain
any logical-line state at all — every `logicalLineStartRow = cursorRow`
special case is gone) and more correct: like MSX, Enter now reads the whole
logical line no matter where within it you pressed Enter.

### 2. Program store: fixed array → packed buffer

Was `ProgramLine lines[60]`, i.e. 60 × (4 + 160) = 9840 bytes of
*always-resident* static RAM regardless of program size, plus two arbitrary
ceilings (60 lines, 160 chars). Now one contiguous buffer of variable-length
records:

```
[uint16 recLen][uint16 lineNumber][text bytes...][NUL]
```

kept sorted by line number, so insert/delete is a `memmove` and walking is
`off += recLen`. 8192 bytes holds roughly 270-400 typical lines — five-plus
times the old ceiling in *less* RAM (measured: 51.6% → 51.1%, −1576 bytes).
Sized conservatively rather than for maximum capacity, deliberately: the BLE
stack needs a 20KB contiguous allocation at connect time and we have already
been on the wrong side of that once.

Two behaviour fixes came with it: storing a line now reports `?Out of memory`
instead of failing silently when full, and line numbers outside 1..65535 are
rejected with `?Line number out of range` rather than being truncated into
the uint16 field.

Also note `PROGRAM_TEXT_BUFFER_SIZE` (4096) is now the *binding* limit, below
the program buffer itself — a program can be stored that then fails to SAVE
or RUN because its serialised/label-rewritten form doesn't fit. Left as-is on
purpose: that whole intermediate whole-program-text step disappears with the
interpreter swap, since a line-numbered interpreter runs the stored program
directly.

### Host tests

Added `test/` with `run_tests.sh` and a test for the packed buffer, compiling
the real `program_store.cpp` unmodified (it only uses standard headers).
Covers ordering, in-place grow/shrink, blank-deletes, uint16 boundary line
numbers, sequential vs. random `GetByIndex` (the sequential-access memo has
its own failure mode), mid-program deletes, serialise round-trip, the
GOTO/GOSUB label rewriting including the string-literal exclusion, buffer-full
refusal, and over-long-line truncation.

Runs in about a second. This is a direct response to how this project has
actually been spending its time: hardware debugging loops have been by far
the most expensive thing here (the phantom-RIGHT hunt cost hours and dozens
of build/flash/log cycles for one bug), so anything testable on the host
should be caught on the host first. The `memmove`-based insert/delete in
particular is exactly the kind of code that fails subtly and would have been
miserable to diagnose through a serial log — and the suite did immediately
flag one real behaviour (the rewriter upper-cases `gosub`), which turned out
to be harmless but was worth knowing rather than discovering on device.


## TinyBasic patch set: fetched, patched, compiling and linking

Built the patch set for swapping My-Basic out for Stefan Lenz's IoT BASIC.
`patches/tinybasic/` now fetches upstream at a pinned commit, patches it, and
`editor/src/tb_runtime.cpp` implements its runtime against this firmware's own
screen terminal and SD card. It compiles and links; nothing calls it yet.

### Which upstream variant, and why it matters

Upstream ships the interpreter twice -- `Basic2/Posix/basic.c` and
`Basic2/IoTBasic/IoTBasic.ino` -- differing by 82 lines. Took the **Posix**
one despite this being an Arduino-framework build, because it is plain portable
C with no Arduino dependencies (confirmed by compiling it standalone on the
host), which makes it usable as a library. The Arduino variant is a *sketch*,
and its companion `runtime.cpp` is 6243 lines of display/keyboard/sensor
drivers we'd spend the integration fighting rather than using. The Posix
runtime (2212 lines) served as the reference for what a minimal runtime looks
like, but isn't used either -- ours replaces it.

### The runtime contract, found by experiment rather than reading

Rather than trying to infer the interface from documentation, compiled
`basic.c` with no runtime at all and diffed its undefined symbols against what
upstream's runtime defines. That gives the contract exactly: **76 symbols**.
Turning off the feature flags we don't want (`01_configure_language.py`) only
took it from 84 to 76 -- the rest are referenced from dispatch tables that
compile in regardless of features, so they must exist even though the
configured language set can never reach them.

Splitting those 76: roughly 39 are real work (character I/O, the filesystem,
timing, scheduling, memory reporting) and the rest are peripherals this device
doesn't expose to BASIC (GPIO, analog, MQTT, RTC, printer, EEPROM, profiling
hooks) which are stubbed. A late correction: eleven of the names turned out to
be **variables**, not functions (`id`, `od`, `ioer`, `charcount[5]`,
`breaksignal`, ...) that the runtime is expected to define. `charcount` in
particular is not incidental -- it's the per-device output column, and it's how
the interpreter implements `PRINT`'s comma tab stops, so `tb_runtime.cpp`
maintains it in `outch()` rather than leaving it at zero.

### Four patches, and what each one is actually for

1. **`01_configure_language.py`** -- explicit feature set instead of upstream's
   board-size heuristics, which on an ESP32-C3 would enable everything
   including Wi-Fi, MQTT, sensors and camera. Keeps what makes this feel like
   MSX BASIC: `HASMSSTRINGS` (`LEFT$`/`RIGHT$`/`MID$`/`ASC`/`CHR$`) and
   `HASDARTMOUTH` (`DEF FN`, `ON..GOTO/GOSUB`, `READ`/`DATA`).
2. **`02_rename_entry_points.py`** -- `setup()`/`loop()` collide with the
   firmware's own, so they become `basicSetup()`/`basicLoop()`, and upstream's
   `main()` is removed. This one bit: the first version cut from `#ifndef
   ARDUINO` to the *first* `#endif`, but there's a nested `#ifdef HASARGS`
   inside `main`, so it sliced the block in half and left dangling code. The
   error surfaced far away as "expected identifier before 'while'". Now counts
   preprocessor nesting properly.
3. **`03_c_linkage_and_config.py`** -- `extern "C"` guards (interpreter is C,
   our runtime must be C++ to talk to the screen editor and SDCardManager);
   real `stdint.h` in place of the Posix variant's hand-rolled Arduino-compat
   typedefs, which collide with the genuine ones here ("conflicting declaration
   'typedef unsigned int uint32_t'"); and a fixed `MEMSIZE` of 16KB rather than
   upstream's `0` = "grab most of free RAM", which is precisely what must not
   happen on a device where BLE needs a 20KB contiguous block at connect time.
4. **`04_library_build_flags.py`** -- a `library.json` scoping this project's
   warnings-as-errors away from third-party code. Same decision as the My-Basic
   `#pragma` wrapper, but cleaner: PlatformIO applies the flags to that
   directory only.

### Verifying it actually links

Compiling isn't proof: with nothing referencing the interpreter, the linker
drops the whole object and never checks the contract. Confirmed this was
happening -- `basic.c.o` was a 973KB object, and `nm` on the final ELF found
zero interpreter symbols.

Forced a real link with a temporary call. First attempt still didn't work,
because an unreachable function gets removed by `--gc-sections`; a
`volatile`-guarded call does the job, since the optimiser can't prove it's
never taken. Result: **links clean, no undefined references**, at a cost of
~21KB RAM (52.3% -> 58.9%) and ~40KB flash. The only symbols `basic.c` still
wants are `atan`, `millis` and `trunc`, all from libm/Arduino at final link.

The probe was then reverted, so the firmware on the device is unchanged and
still the working My-Basic build.

### What's left

The switchover itself. In rough order:

- Implement `consins()` -- currently a stub returning "no line available". This
  is the real join between the two worlds: upstream reads a line by driving its
  own console, while here the console is a character grid with a freely movable
  cursor, wrapped logical lines and its own Enter semantics (`screen_editor.h`).
  Line editing stays ours; `consins()` should hand over a completed line.
- Point `SCREEN_EDITOR`'s Enter handling at `basicLoop()` instead of
  `mbBridgeRunDirect()`/`mbBridgeRunProgram()`.
- Decide what happens to `program_store.cpp`. The interpreter has its own
  tokenised program memory, so the packed buffer becomes redundant for
  *storage* -- but `LIST`/`SAVE`/`LOAD`/`VC` are all built on it. Cheapest path
  is probably to let the interpreter own the program and re-point those at its
  own `LIST`/`SAVE`/`LOAD`, which is also the more authentic behaviour.
- Retire `mb_bridge.cpp`, `program_store`'s `rewriteGotoGosub` (the `L%d:` label
  hack exists only because My-Basic has no line numbers) and the MyBasic
  library once nothing references them.
- Reclaim RAM: dropping My-Basic and `PROGRAM_TEXT_BUFFER_SIZE`'s three 4KB
  buffers should more than pay for the interpreter's 16KB `MEMSIZE`.


## Switchover done: the interpreter owns the program

Handed program storage and every classic command to TinyBasic. My-Basic is out
of the binary entirely (flash 31.7% -> 27.5%).

The question that prompted this: *why was My-Basic still there, if the program
was already in TinyBasic's structure?* It wasn't -- and that was the whole
answer. Numbered lines were still being intercepted and routed to
`program_store`, so TinyBasic's own program memory sat empty and `RUN` had to
stay with My-Basic because My-Basic was the only thing that could execute what
was in our store. The staging was deliberate (`LIST`/`SAVE`/`LOAD`/`VC` are all
built on `program_store`, so they move as a package or not at all) but it was
half a bridge.

Checked what the interpreter actually provides before deciding what to keep:
its keyword table has `LIST`, `RUN`, `NEW`, `SAVE`, `LOAD`, `CLS`, `CONT`,
`STOP`, `DELETE`, `CATALOG`, `DATA`/`READ`/`RESTORE`, `DEF FN`, `ON`,
`LEFT`/`RIGHT`/`MID`, `EDIT`, `HELP` and more. So `executeLogicalLine()` now
intercepts exactly three things -- `MENU`, `VC` and `SCREEN` -- because those
are this device's rather than the language's, and everything else goes to the
interpreter.

Two things fell out nicely:

- **`CLS` needs no interception.** Its implementation is literally
  `outch(12)`, so handling form feed in the runtime's `outch()` was enough.
- **Break and repaint got simpler than under My-Basic.** The interpreter polls
  `checkch()` after *every* statement and treats `BREAKCHAR` as stop, and calls
  `byield()` in the same place. So Escape/Ctrl+C hangs off `checkch()` and the
  throttled display flush off `byield()` -- no stepped-handler workaround.

`vc_browser` had to change with it: it was still loading through
`screenEditorLoadProgram()` into the now-unused store, which would have left
`RUN`/`LIST` looking at an empty program. It issues `LOAD "name"` to the
interpreter instead.

## The type mismatch that turned every line number into 0

First hardware test of the switchover: program entry worked, but `LIST` showed
every line starting with `0`, and `RUN` failed with `0: Unknown Line Error`
because `GOTO 10` had no line 10 to find. The user's read was that the first
character of each line number was being eaten, in storage and not just in the
listing.

Reproduced it on the host rather than guessing: built a REPL harness around the
patched `basic.c` plus upstream's POSIX runtime, driving it with the exact
sequence `tb_bridge.cpp` uses. Same symptom immediately -- and the harness could
print the interpreter's state, which showed `token == NUMBER` (correct) but
`x == 1.09262e+09` instead of 10.

That number is the tell: 1092616192 is `0x41200000`, the IEEE-754 bit pattern of
the float `10.0`. So `x` held a float and was being *read as an integer*.

Root cause: `basic.h` picks the numeric type from a feature macro --

    #ifdef HASFLOAT
    typedef float number_t;
    #else
    typedef int   number_t;

-- and `HASFLOAT` is defined in `language.h`. `basic.c` includes `language.h`
before `basic.h`; `tb_bridge.cpp` and `tb_runtime.cpp` did not include it at
all. So the interpreter compiled with `number_t = float` while our side saw
`number_t = int`, for the same global. It compiles clean, links clean, and then
`ax = x` reads the float's bits as an integer: 1092616192, truncated to a
uint16 line number, is exactly 0. Both `10` and `20` landed on 0 -- which is why
it looked like a digit had been eaten rather than the number being destroyed.

Fixed by adding `editor/src/tb_interp.h`: one place that includes the
interpreter's headers, in `basic.c`'s exact order, and declares its globals
once. Nothing else includes them directly. The header carries the explanation,
because the failure mode gives no compiler help at all.

Also fixed alongside: a blank line after every entry, because
`executeLogicalLine()` advanced the terminal unconditionally after running a
line -- but storing a numbered line prints nothing, and `PRINT` ends with its
own newline. Now it only advances if the cursor was left mid-row.

## Dialect notes from testing

- `LIST` takes `LIST b,e` for a range. `LIST n` lists **only** line n, unlike
  the previous implementation where `LIST 30` meant "from 30 onwards".
- `PRINT "x"` breaks the line; `PRINT "x";` does not. Classic behaviour, but
  worth stating since the expectation was that consecutive prints would run
  together by default.
- `LEFT$`/`MID$`/`RIGHT$` only accept string *variables*, not literals --
  `A$="teste": PRINT LEFT$(A$,3)` gives `tes`, `PRINT LEFT$("teste",3)` gives
  `Args Error`. This is inherent to the interpreter's in-place string design
  (its own docs note MS-BASIC compatibility is limited for exactly this
  reason): those functions return a reference into a variable, and a temporary
  literal gives them nothing to point into. `LEN`/`ASC`/`CHR$` take literals.
- Variables persist across direct-mode statements, as expected.

## The host REPL harness

Worth keeping: `basic.c` compiles unmodified on the host against upstream's
POSIX runtime, so the interpreter can be driven from a `main()` that replicates
`tb_bridge.cpp` exactly. That is how both the line-number bug and the `LEFT$`
limitation were diagnosed -- in seconds, with full visibility of interpreter
state, instead of build/flash/observe cycles on the device. Given how much of
this project's time has gone into hardware debugging loops, anything answerable
this way should be.


## Second hardware round: SAVE, backspace, stray blank lines, and why CONT fails

### SAVE wrote a 0-byte file

`xsave()` doesn't write to a file handle. It does `od = OFILE` and then
*prints* the program through `outch()`, the same call `PRINT` uses. Our
`outch()` ignored `od` entirely and always wrote to the terminal, so SAVE
listed the program on screen and left an empty file on the card.

Fixed by dispatching on `od`: `OFILE` writes raw bytes to the open output
file, `OSERIAL` does the terminal translation (newline, form feed, the
`charcount` column tracking), and every other channel is discarded since
nothing is attached. Deliberately *not* copied from upstream's version: its
`outch()` ends with `byield()` "for fuzzy OSes", and here `byield()` carries a
`vTaskDelay` and a display flush, so paying that per character would make
output crawl.

### Backspace crossed row boundaries on unrelated lines

`screenEditorBackspace()` went up to the previous row whenever the cursor was
at column 0. That's right for a line that wrapped while typing -- the previous
character genuinely is up there -- but wrong on a freshly started line, where
it walked back into and ate whatever text happened to be above.

The continuation flags added earlier for the logical-line fix already carry
exactly the information needed, so this became a one-condition change: only
cross the boundary when `rowIsContinuation[cursorRow]`.

### Two more stray blank lines

Same cause as the one fixed for program entry: advancing the terminal
unconditionally after a command that ends its own output. `SCREEN n` (which
clears the screen and homes the cursor) and the `FILES` alias both did it.
Both now use the same "only advance if the cursor was left mid-row" check.

### CONT: an interpreter bug, diagnosed on the host

`CONT` after a Ctrl+C break reported "20: Syntax Error" on a two-line program.
Reproduced on the host harness by adding a counter to `checkch()` that fires a
break after N statements, then sweeping N -- which made it deterministic:

    break at here=3  -> CONT resumes cleanly
    break at here=4  -> "10: Syntax Error"
    break at here=12 -> CONT resumes cleanly
    break at here=13 -> "20: Syntax Error"

The cause is in the interpreter's own execution loop. It keeps one token lexed
ahead: `token` holds the current one while `here` already points *past* it.
The break path returns without preserving `token`, and `CONT` then calls
`nexttoken()`, which re-reads from `here` -- so one token is silently skipped.
When the skipped token is a line number it costs nothing, because the
statement loop skips those anyway (`case LINENUMBER: nexttoken()`), which is
why half the break positions resume fine. When it is a statement keyword,
execution resumes inside that statement's arguments and fails to parse.

This is upstream's, not ours: the same flow exists in its own `basicLoop()`,
and the reproduction uses upstream's own runtime. Fixing it means saving
`token` at break time and restoring it on `CONT` -- a change to interpreter
internals rather than to configuration or glue, so it hasn't been made
unilaterally. Documented as a known limitation; `RUN` restarts fine.

### Dialect confirmations from this round

- `LIST b,e` lists a range; `LIST n` lists only line n. The user's read is
  that this is the better behaviour -- a single argument now unambiguously
  means "that line" -- and it matches MSX, where "from n onwards" was its own
  form. `LIST 20,200` on a program ending at line 30 simply stops at 30.
- `PRINT "x"` breaks the line and `PRINT "x";` does not, exactly as classic
  BASIC. Not a bug, just a forgotten detail.
- Variables persist between direct-mode statements, as expected.


## Phantom RIGHT: a concrete, testable hypothesis at last

New evidence from the user, gathered across this project and the parallel
MicroWriter one:

- It reproduces on a **second, different X4**, so it is not one unit's wear.
- It does **not** happen on CrossPoint, CrossInk or CPR-vCodex.
- It **does** happen on original MicroSlate, in reduced form -- only at boot
  when arriving from an OTA switch, never on sleep/wake within the same slot.

So it is something this firmware lineage does that the readers don't. Compared
the two directly:

| | this firmware | CrossPoint / CPR-vCodex |
|---|---|---|
| framework | `arduino, espidf` | `arduino` |
| power management | `esp_pm_configure()`, 10-80MHz DFS + auto light sleep | none at all |

The readers never call `esp_pm_configure`. We enable dynamic frequency
scaling across an 8x range (80MHz down to 10MHz) plus automatic light sleep.

That is a strong candidate, because the ESP32-C3's SAR ADC sampling is timed
off the APB clock. A conversion taken during or immediately after a frequency
transition can come out wrong -- and on this resistor ladder RIGHT occupies
the extreme bottom of the range (anything under 750 of 4095), so *any* reading
biased low classifies as "RIGHT pressed". Nothing else on the ladder is that
exposed.

It also accounts for everything previously unexplained:

- Absent on the readers, because they have no DFS.
- Present across different units, because it is firmware, not hardware.
- Worse at boot after an OTA switch -- a different clock/PM state path.
- Worse after the BASIC integration -- more CPU activity, more transitions.
- And crucially, why **raising the debounce made it worse**: a longer window
  integrates more wrong samples rather than filtering them. Debounce filters
  brief spikes; these are systematically wrong conversions during transitions,
  which is a different failure entirely. That result always looked backwards
  and now makes sense.

Decisive experiment, cheap to run: pin `min_freq_mhz = max_freq_mhz = 80` and
`light_sleep_enable = false`, then use it normally. If the phantom presses
stop, it's confirmed. The cost is battery life, so this is a diagnostic rather
than the fix -- the proper fix, if confirmed, is to hold an `esp_pm_lock`
across ADC reads so the frequency can't move under a conversion, leaving DFS
enabled everywhere else.

Not run yet; recorded here so the next session can start from it rather than
from the dead ends.

## Round three of hardware testing

- **VC's return to the terminal.** Two problems, both fixed. It cleared the
  typed "VC" line, which made returning look as though the terminal had been
  wiped -- now the command stays visible, consistent with every other command
  since the interpreter took over, and it gives the "Loaded ..." line context.
  And it left a blank line, the same unconditional-advance pattern already
  fixed three times elsewhere; it now uses the shared cursor-column check.
- **VC across SCREEN modes** works. On SCREEN 0 (32 columns) the footer
  crowds out the detail text, which the user judged not worth addressing --
  it's informational only.
- **SAVE** confirmed working after the `od` dispatch fix.
- **File sizes differ between a hand-written .txt and a SAVEd program** (48
  vs 35 bytes for the same two-line program). Expected: SAVE re-emits from the
  interpreter's tokenised form, so it is canonical -- keywords upper-cased,
  single spaces, LF line endings. A file written on a PC typically carries
  CRLF (one extra byte per line), possibly a trailing blank line, and whatever
  spacing was typed. The saved form being smaller and consistent is the point;
  it round-trips identically from then on.

## Round four: output speed, EXIT, and the boot line

- **`EXIT` as an alias for `MENU`.** Both names now leave the screen editor.
  A machine with exactly one way out should answer to whichever word the
  user reaches for.
- **Boot identification line.** `tbSetup()` prints
  `FSP MicroBASIC v0.3 for XTeink X4` before calling `basicSetup()`, so it
  lands above the interpreter's own multi-line greeting: whose computer it is
  first, which BASIC second -- the order those machines actually used. (The
  initials were added later, and are the point of the line: these machines
  announced whose they were.)
- **Screen output was crawling** (several seconds per printed line from a
  `PRINT`/`GOTO` loop). This was a bug of ours, not the interpreter's, and
  worth writing down because the shape of it is easy to repeat.

  `byield()` throttled the e-ink flush by wall clock:

      if (millis() - lastFlush < FLUSH_INTERVAL_MS) return;
      lastFlush = millis();
      screenEditorFlushDisplay();

  The timestamp was taken *before* a call that blocks for ~700ms. So the
  interval was being measured from the start of one refresh to the start of
  the next, and since the refresh itself outlasts the interval, the very
  first `byield()` after a refresh returned already qualified for another
  one. Effectively: refresh, run one statement, refresh, run one statement.
  The display was busy essentially all the time and the program advanced at
  the speed of the panel.

  Two changes. `lastFlush = millis()` now runs *after* the refresh, so the
  interval means what it reads as -- idle time between refreshes. And a
  `termDirty` flag, set in `outch()` and cleared on flush, means a program
  that computes without printing pays nothing: no refresh is scheduled for a
  screen that hasn't changed. `FLUSH_INTERVAL_MS` is 400, so the panel now
  spends roughly a third of its time refreshing rather than all of it.

  The yield itself is separate and unthrottled by time: every 16th
  `byield()` does a `vTaskDelay(1)`, which is what keeps the watchdog fed.
  Tying the yield to the flush interval was the other tempting shape here,
  and it would have reintroduced the freeze the flush is unrelated to.

## Round five: keyboard input, LOCATE, and a game to test with

The user asked for "a definitive test": one BASIC program that exercises
everything at once. Writing it turned up two things the environment could
not do yet, both of which are the difference between a language you can
compute with and one you can write a screen program in.

- **The keyboard was unreachable from BASIC.** `availch()`/`inch()` returned
  0, so `GET`, `@A` and `@C` were all dead. The keys were being *thrown away*
  on purpose: `inputConsumeBreakPending()` drained the whole queue looking
  for Escape or Ctrl+C and discarded everything else, because until now the
  only reason to look at the queue during a RUN was to stop the program.
  It now sorts instead of discards -- breaks on one side, a small ring of
  characters on the other -- and the runtime's `availch()`/`inch()` read that
  ring. Arrow keys have no ASCII, so they arrive as the MSX codes 28/29/30/31
  (right/left/up/down), matching the machine this environment's SCREEN modes
  already follow.

  The ring holds 16 and drops the *newest* key when full. A game wants the
  key being held down now, not a backlog of everything pressed while it was
  repainting.

- **LOCATE printed garbage.** It is not a runtime call upstream: `xlocate()`
  implements it by writing a VT52 `ESC Y row col` sequence through `outch()`,
  and a runtime that doesn't decode that prints four stray characters. A
  four-state decoder in `outch()` fixes it, and it is what makes
  screen-oriented BASIC possible here at all -- a program can repaint the
  cells that changed instead of scrolling, which given a ~700ms panel refresh
  is the difference between a game and a listing. Both operands are 1-based
  and biased by 32, so `LOCATE 1,1` is the top-left cell, and the argument
  order is column-then-row, as MSX had it. Any other escape sequence is
  swallowed rather than printed.

- **Break is now announced.** The interpreter's break path is silent -- it
  stops and returns to the prompt -- which on a screen already full of a
  program's output leaves nothing to distinguish "I stopped it" from "it
  finished". `checkch()` now prints `Break in <line>` before returning
  BREAKCHAR, which has to happen there rather than after the fact: `st` still
  says a program is running, so the line number is still recoverable.

- **`DIR`** joins `FILES` as an alias for the interpreter's `CATALOG`.

- **`examples/pacman.bas`** is the test program: an 11x21 maze in DATA, a
  player, a chasing ghost, pellets and a score, in about 90 lines. It uses
  `DIM`/substring read *and* write as a maze buffer (this BASIC's strings are
  in-place, so `M$(P,P)` is both the getter and the setter, and there are no
  string arrays), `READ`/`DATA`, `GOSUB`, `GET`, `LOCATE`, `@T` for frame
  timing, `RND`/`INT`/`ABS`/`SGN`, and `AND` in conditions. The logic was
  validated on the host REPL harness before it ever reached the device, with
  `GET` replaced by scripted moves and the VT52 output read back as ANSI.

  One thing it cannot do: call `SCREEN`. That command is the firmware's, not
  the interpreter's, so it only works typed at the prompt -- the program says
  so in a REM. Making it available to programs would mean adding a token to
  the interpreter, which is a patch, not an integration.

### HELP <command>

Reported as printing only the command name. That is upstream's own stub:
`xhelp()` prints the token and `": "` and nothing else -- the per-command
help texts were never written. Not an integration problem. Adding them would
mean a string table for ~150 keywords in flash.

## Menu: two collections instead of one

The prose editor was reachable as "New Program", which was wrong in both
directions: it writes prose into the notes folder, and there was no way to
get at `/MicroBASIC/programs` from the UI at all. The menu now carries both
pairs -- `Browse Programs` / `New Program` over `/MicroBASIC/programs`,
`Browse Files` / `New Note` over the notes folder, as MicroWriter had them.

The browser and the editor are unchanged; what moved is *where they look*.
`file_manager` gained a collection descriptor (folder, extension, fallback
base name, label, whether to filter by extension) and the menu picks one
before entering either screen. Programs are listed unfiltered on purpose:
BASIC's SAVE stores under exactly the name typed with no extension forced
on, so a folder of perfectly good programs can contain no `.bas` at all.
`.tmp` and `.bak`, which the save path creates, are excluded from both.

This closes the loop the SD layout was always aiming at: a program written
in the prose editor lands where `LOAD` looks for it, and one typed at the
BASIC prompt can be opened in the editor.

## WiFi scan failing

Reported: Sync errors out scanning for networks. The scan code is unchanged
from MicroWriter, where it works, so the difference is the environment
around it. Two candidates, and the fix addresses the first while making the
second visible:

1. **Power management.** This firmware runs automatic light sleep with
   10-80MHz DFS. A scan is a timed sequence of channel hops; a core that may
   drop to 10MHz or sleep between them is the wrong place to run one.
   `wifiSyncStart()` now pins the clock at 80MHz with light sleep off for as
   long as the sync screen is open, and hands it back on the way out. (This
   is the same power configuration implicated in the phantom-RIGHT bug
   above, which is worth noting: it is now suspect in two places.)

2. **Contiguous heap.** Bringing up the WiFi stack is a large allocation, and
   this device has run out of contiguous heap before -- BLE needed 20KB and
   the largest free block was 8704. `scanNetworks()` reports that kind of
   failure immediately, through its own return value, rather than through
   `scanComplete()`, and both used to surface as the same "Scan failed".
   They are now distinguished, and the failure message carries the largest
   free block so the cause is readable off the screen rather than needing a
   serial cable.

## Filenames: who owns the name

Reported: creating a program named `novo.bas` produced `novobas`, and the
browser showed neither `.bas` nor the `.txt` of a file already in the folder.
Both come from the same assumption, inherited from MicroWriter: that the
filename is an implementation detail and the *title* is what the user deals
with. `titleToFilename()` dropped every non-alphanumeric character, the dot
included, then appended `.txt`; `filenameToTitle()` stopped at the first dot
and prettified what was left.

That is right for notes and wrong for programs, and the reason is not
cosmetic: a program's name is something the user *says to the interpreter*.
`LOAD "pacman.bas"` has to find the file, so what the browser shows must be
what LOAD takes. The collection descriptor gained a `rawNames` flag:

- Programs show and edit the filename itself. The dot is a legal character,
  a typed extension is kept, and the default is supplied only when none was
  typed -- so `novo.bas` stays `novo.bas`, `novo` becomes `novo.bas`, and
  `novo.b` stays `novo.b`. Still lowercased, since the SD card holds
  everything lowercase and LOAD should never have to guess at case.
- Notes keep MicroWriter's behaviour exactly.

The title-edit screen says which one it is ("Edit Filename" over "Program
filename (e.g. pacman.bas)" versus "Edit Title" / "Note title"), and a new
program is prefilled with `untitled.bas` rather than `Untitled` -- seeing the
extension in the field is what tells the user it is theirs to change.

One bug fell out of making the field show the real filename.
`deriveUniqueFilename()` bumps a name to `_2` when it already exists, and the
file being renamed always exists -- so confirming a rename *without changing
anything* renamed the file to `name_2`. That was rare when the field held a
prettified title and is the common case when it holds the exact filename, so
it now takes an `except` argument naming the file that doesn't count as a
collision. It also splits at the last dot rather than assuming the
collection's own extension, so `game.b` collides into `game_2.b`.

## Removing the My-Basic layer

The WiFi scan failure reported above turned out to be the heap, and the
message added for it said so: "Radio busy (heap 7K)". Bringing up the WiFi
stack needs a contiguous block and there was not one.

The obvious place to find it was the layer the interpreter replaced and
nothing had deleted:

- `program_store.cpp` -- an 8192-byte packed program buffer. The interpreter
  has owned program storage since the swap.
- `mb_bridge.cpp` -- the My-Basic bridge, already gone from flash (the linker
  had garbage-collected it) but still in the tree.
- `screen_editor`'s `SAVE`/`LOAD` implementation and its two 4096-byte
  buffers -- `SAVE`/`LOAD` are the interpreter's now, streaming straight to
  the SD card.
- `input_handler`'s LIST/FILES `MORE?` pagination, including a 3200-byte
  filename table. The interpreter's own LIST and CATALOG scroll instead, as
  MSX did; nothing had started the paging state machine since.

Every one of these was already flagged `defined but not used` by the
compiler. Static RAM went from 180472 to 169048 bytes -- 11.4KB back, and
because static `.bss` sits below the heap, that is 11.4KB of *contiguous*
heap rather than 11.4KB scattered.

Whether that is enough is a hardware question; the failure message now
reports the number either way. If it is still short, the next candidates are
the text editor's 16KB clipboard (allocate on demand) and the interpreter's
16KB `mem` (tunable in patches/tinybasic/03, at the cost of program space).
Note also that MicroWriter, where sync works, does not carry the
interpreter's `mem` at all -- 16KB of the difference is simply the price of
having a BASIC in the same binary.

## Sync, diagnosed from the IDF log

The heap work above got the scan running and the device onto the network,
which turned the remaining problems into ones a serial capture could answer.
Worth recording that the capture itself needed a detour: `-DRELEASE_BUILD` is
set in platformio.ini, so every `DBG_PRINTF` in this firmware compiles to
nothing. None of the instrumentation added for this appears on the wire. The
ESP-IDF log is a separate mechanism and was still there, and it turned out to
say everything that mattered.

**The page crawled, and arrived truncated.** One line explains both:

    wifi:pm stop, total sleep time: 65986964 us / 76529859 us

The radio was asleep for 86% of the session. WiFi's own modem power save is
separate from the CPU light sleep we had already pinned off, is on by
default, and parks the radio between DTIM beacons -- 102ms apart on this
network. Every TCP round trip waited for one.

That is the slowness directly, and the truncation indirectly:
`WiFiClient::write()` retries a fixed number of times and then returns short,
and `sendContent_P` does not loop. A 9.6KB body handed to it in one call ran
out of retries partway through. Nothing reports that -- the HTML that did
arrive rendered fine, and what did not arrive was the `<script>` at the end
of the file. Hence the exact symptoms reported: the page appeared, the Notes
tab showed no notes, and clicking "BASIC programs" did nothing, because
`selectTab` had never been defined.

Fixes, in order of how much they matter:

- `WiFi.setSleep(false)` for the duration of the sync screen. Sync is a
  foreground activity of at most a few minutes; there is nothing to save
  power for while the user is standing there waiting for a file list.
- The page is sent one 1440-byte segment at a time. Each chunk gets its own
  retry budget, so a stall costs a pause instead of the rest of the file.
- The idle timeout went from 60s to 5 minutes. Browsing a file list is mostly
  reading, and 60s was dropping the connection while the user was still
  deciding what to download. (The "sync complete, no change" the user saw
  after a minute was this timeout, not a sync that had decided nothing
  changed.)

Also visible in the log, and left alone for now:

    W wifi:Error! Should use default active scan time parameter for WiFi
      scan when Bluetooth is enabled!!!!!!

Coexistence is compiled in (`CONFIG_SW_COEXIST_ENABLE=y`) and the scan does
use default timings, so this is the stack noting the constraint rather than a
failure. Worth remembering if scanning gets flaky again.

**BLE keyboard dropping.** Reported as losing sync regularly, with timeout or
power saving suspected. The likelier cause is the same heap the WiFi stack
was short of: the BLE connect task needs a 20480-byte contiguous allocation
at connect time, and when it cannot get one, `xTaskCreate` fails silently and
the keyboard simply never reconnects -- this exact failure is already
documented above, from when the largest free block was 8704. Static RAM is
now 27.8KB lower than when that was happening, which should give it room.
Not yet confirmed on hardware.

**Password not being saved.** Not reproduced or explained. The SAVE_PROMPT
state is entered on any connection that did not use a stored password, and
its screen is drawn with explicit instructions ("Save password? Enter/Up:
Yes  Down/Esc: No"), so the flow looks right on inspection. The open question
is whether that screen is being reached and dismissed, possibly by a
keystroke lost to the BLE dropout above. Needs a hardware observation before
changing anything.

## The reboot on entering Sync: my own fix

Worth writing down in full, because the log said it in one line and the
mistake is an easy one to repeat.

    wifi:Set ps type: 1          <- the default
    wifi:Set ps type: 0          <- WiFi.setSleep(false), added last round
    E wifi:Error! Should enable WiFi modem sleep when both WiFi and
      Bluetooth are enabled!!!!!!
    abort() was called at PC 0x420c849b on core 0

WiFi and BLE share one radio here, and coexistence requires the WiFi side to
keep sleeping so the BLE side gets airtime. Turning modem sleep off is not
"less power saving", it is an abort. And there is no way around it by
shutting BLE down for the duration: the keyboard is BLE and the sync screen
needs it to pick a network and type a password.

So modem sleep stays, and the fix for the slow, truncated page is entirely
the chunked send: with the AP's DTIM period of 1 the station wakes every
beacon (~102ms), which is a latency to design around, not a stall. Sending
the page one 1440-byte segment at a time gives each segment its own retry
budget inside WiFiClient::write(), which is what a body larger than the
retry budget needed.

Also fixed the teardown order. `pinClockForRadio(false)` ran *before*
`WiFi.mode(WIFI_OFF)`, so the radio was being torn down with 10MHz DFS and
light sleep already back on, and it did not always finish:
"E wifi:timeout when WiFi un-init, type=4". The clock is handed back last now.

## "Save password?" answering itself

Reported first as "the save screen never appears", then -- the observation
that cracked it -- as "it appeared briefly and vanished before I could
save". So the state was being entered correctly all along and something was
answering it.

Two things make this state different from every other screen in the sync
flow, and both matter:

1. It is entered by *something finishing* (the connection succeeding), not by
   the user pressing anything. Whatever is in the input queue at that moment
   was typed at a different screen -- the Enter that submitted the password,
   or a keyboard auto-repeat of it -- and Enter on this screen means "yes,
   save". It answered itself.
2. The panel takes ~700ms to actually display the question. Any key in that
   window answers something the user has not read. This device also generates
   spurious button presses (see the phantom RIGHT section above), and a
   prompt that can be dismissed before it is visible is where that does the
   most damage.

Both prompts (SAVE_PROMPT and FORGET_PROMPT, which has the same shape) now go
through `openPrompt()`: it discards the queued input on entry and ignores
keys for 900ms, one refresh plus margin. Confirmed on hardware: the password
saves, and a second entry into Sync auto-connects to the known network
without scanning.

Separately, and found while reading this path: `usedSavedPassword` and
`autoConnectAttempted` are statics that outlive a sync session, and neither
`beginScan()` nor `wifiSyncStart()` reset them. Answering "yes" to
FORGET_PROMPT rescans, which left `usedSavedPassword` true from the
auto-connect that had just failed -- and `pollConnection()` only offers to
save when it is false. Not the bug the user hit (the manual path sets it
false on the way to the password screen), but it is the same bug one step
over, so both are reset now.

## Cursor during RUN

Reported as a cursor block permanently stuck to the right of the ghost. The
terminal draws a cursor at the current position, and a program that repaints
cells in place leaves that position wherever it last printed.

A cursor means "waiting for you to type", and during RUN nothing is. It is
now hidden whenever the interpreter has control -- `tbIsRunning()`, set
around the statement dispatch in tb_bridge, so it covers LIST and CATALOG
too, not just RUN.

## Pacman: continuous movement, and a ghost that does not pace

Two changes from playing it:

- The player keeps moving in the last direction until a wall, rather than
  stepping once per key. The arrow now sets a *wanted* direction; each frame
  adopts it if that way is open and otherwise carries on. This is how the
  original works and it is what makes it playable at one frame per e-ink
  refresh -- you steer ahead of time instead of racing the panel.
- The ghost may not reverse. Chasing on the dominant axis and falling back to
  random made it pace back and forth between two cells whenever the direct
  route was blocked, which was visible in the host trace. Refusing a reversal
  unless it is the only way out of a dead end costs one comparison and turns
  the pacing into a patrol.

## INPUT

The last statement that did not work, and the one whose absence made the
whole thing feel like a demo rather than a BASIC. It is also the only place
where the interpreter asks for something this integration was built to
avoid: everywhere else the firmware hands it a finished line and it never
waits on a key.

`INPUT` calls `ins()` -> `consins()` for both its numeric and its string
form (`ins(s.ir - 1, maxlen)` for strings -- note the `- 1`, which is the
length byte landing in the byte before the string data). Ours has to block,
because it is called from inside `statement()`, inside `tbExecuteLine()`,
inside loopTask. For as long as it runs, `loop()` is not running: nothing
else repaints the screen, feeds the watchdog, or drains the keyboard. All
three happen inside the read loop.

It is deliberately its own small line editor rather than reusing the screen
editor's. `INPUT` reads a *value*, not a program line, and the screen
editor's cursor movement, logical-line continuation and Enter semantics
would let the user wander off into the rest of the terminal mid-statement.
Characters echo as typed, backspace erases, and everything else is dropped
-- including the arrow codes, which would otherwise be stored as text now
that the key ring delivers them.

Three details that would each have been a silent bug:

- **The buffer contract.** Text at `b[1..z]`, NUL at `b[z+1]`, length in
  `b[0]`. Copied from upstream's own POSIX `consins()` rather than inferred,
  the same discipline that the `ibuffer` prefix in tb_bridge needed after
  getting it wrong once.
- **The prompt has to be flushed before blocking.** `showprompt()` has
  already gone through `outch` when `consins()` is entered, but `byield()`
  would sit on it for up to 400ms -- the user would be staring at a screen
  that had not yet asked them anything.
- **Break.** A break arriving during `INPUT` is reported through the buffer,
  which is how the interpreter expects to hear it: `innumber()` returns -1
  on seeing `BREAKCHAR` and `INPUT` stops the program. It is also latched,
  so the run loop's own `checkch()` fires on the next statement -- the
  string branch has no equivalent of `innumber`'s check and would otherwise
  just store the character and carry on.

Backspace also had to be added to the key ring (`hidToAscii` returns 0 for
it, and it was not one of the arrow codes being mapped). A program using
`GET` now sees 8 for it, which is correct.

The interpreter side was verified on the host harness first -- prompt forms,
`INPUT "NOME? ";A$`, bare `INPUT A`, numeric conversion -- so what remained
to be got right on the device was only the runtime half.

Flashed and awaiting the hardware pass; see docs/HARDWARE_TESTS.md section 12.

## Phantom buttons: still RIGHT, and the evidence got sharper

An observation that first looked like a new symptom and turned out to be a
sharper reading of the old one. Reported as extra *downward* movement: the
main menu scrolling past Sync into the reader entries, and the WiFi network
list sitting one line below the intended network by the time Enter is
pressed.

That is the same phantom RIGHT. In this UI the d-pad's RIGHT moves a menu
selection **down** and LEFT moves it **up** -- so a spurious RIGHT reads as
a spurious "down", and no other button is implicated. (Recorded because the
first draft of this section guessed the fault had spread beyond one button.
It had not; the mapping simply makes one button look like a direction.)

This *strengthens* the ADC hypothesis rather than complicating it. RIGHT
occupies the extreme low band of the shared resistor ladder (<750 of 4095),
so a reading biased low lands on RIGHT specifically -- which is exactly what
a conversion sampled across a DFS frequency transition would produce, since
the SAR ADC is APB-clock-timed. It also explains why more debounce made it
*worse*: debounce averages more samples into a window where the clock is
moving, not fewer.

What it costs is no longer cosmetic: entering Sync took several attempts,
because the selection kept sliding down to cpr-vcodex before Enter landed.

The decisive experiment is still the one recorded above -- pin
`min_freq_mhz = max_freq_mhz = 80` and `light_sleep_enable = false`, then
use the menus normally. Note that the sync screen already does exactly this
for as long as it is open (`pinClockForRadio`), which makes it a ready-made
A/B: if the phantom presses stop once the sync screen is up but not before
it, that is the answer without writing any new code.

The guard now protecting the sync prompts -- discard queued input, ignore
keys for 900ms after a screen appears on its own -- exists because of this,
and is the right shape of defence for any screen a spurious press can
answer. It is a mitigation, not a fix.

## Accents in INPUT, and why BASIC strings are Latin-1

`INPUT` took letters but not accents. The dead key engine was there --
`dead_keys.h`, the same US-International machine both editors use -- but the
program key ring was reading `hidToAscii` directly and never went through
it, so a dead key arrived as a bare quote or tilde.

Routing the ring through `deadKeyProcess()` is the fix, but it forces a
decision the editors never had to make, because they deal in UTF-8 and the
interpreter does not: **what is one character?**

BASIC strings are byte arrays. `LEN`, `MID$`, `LEFT$` and the in-place
substring assignment that `examples/pacman.bas` uses as a maze buffer all
index *bytes*. If an accented character went in as its two UTF-8 bytes,
`LEN("olá")` would be 4, `MID$(A$,3,1)` would return half a character, and
the failure would be silent and confusing in exactly the way this project
has already been bitten by once (the `number_t` mismatch).

So the ring stores **Latin-1**: one byte per character. That is not a
shortcut, it is what the machines this imitates actually did -- a
single-byte character set, `ASC` and `CHR$` meaning something simple -- and
every character the US-International layout composes (the acute, grave,
circumflex and tilde vowels, cedilla, umlaut, ñ) is in it. Codepoints above
0xFF are dropped; nothing the layout can produce reaches there.

Three places had to agree on it:

- `pushProgramKey()` converts the composed codepoint to a byte.
- `consins()` accepts 0xA0-0xFF as text and echoes it as a codepoint --
  Latin-1 and Unicode are identical in that range, so the byte *is* the
  codepoint. The C1 range 0x80-0x9F is dropped with the other controls.
- `outch()` converts back on the way out: the terminal wants UTF-8, and this
  range is two bytes. Before this it wrote the raw byte, which is not valid
  UTF-8 and rendered as nothing useful.

One consequence worth stating rather than discovering later: a program
`SAVE`d with accented string literals is a Latin-1 text file on the SD card,
not UTF-8. It round-trips through `LOAD` exactly, and it is still plain text
readable without this firmware, but a modern editor opening it will need to
be told the encoding. The alternative -- UTF-8 on disk -- would mean either
breaking `LEN` or transcoding on every `SAVE`/`LOAD`, and the file is
overwhelmingly ASCII in practice.

Also reset the dead key state in `inputFlushProgramKeys()`, which runs before
each command: an accent held down when the last command ended is not an
accent on the first character of the next one.

### The typed line was throwing them away first

`INPUT` then took accents and `PRINT "acao"` with a cedilla still printed
`a??o`, because they are different paths and only one had been fixed. A
typed line reaches the interpreter through
`screenEditorGetLogicalLineText()`, which read the grid's codepoints and
substituted `'?'` for everything above ASCII. Its own comment explained why:
"command/line-number parsing never needs more than that". That was true when
a typed line was only ever a command like `LIST` or `MENU`, and stopped
being true the moment the line could be BASIC source containing a string
literal. The substitution happened before the interpreter ever saw the line,
so no amount of correctness downstream could recover it.

It now emits Latin-1 bytes, the same encoding as every other path.

Verified on the host harness that the interpreter itself is not the problem:
feeding it a line with raw 0xE7/0xE3 bytes inside a string literal prints
them back unchanged and reports `LEN` of 3 for a three-character accented
word. Nothing in the tokeniser touches bytes between quotes. So the encoding
holds end to end -- keyboard, grid, typed line, interpreter, terminal -- and
the only place it is visible from outside is a SAVEd file, as noted above.

## Accented filenames: SdFat rejects them outright

`SAVE` failed with `File Error` only when the *name* had an accent -- the
content was never the problem. Found by making the failure self-describing
rather than by reading more code: `File Error` alone cannot tell a missing
card from a missing directory from a name nobody can open, and a release
build has no serial log (`-DRELEASE_BUILD`). `ofileopen()`/`ifileopen()` now
print the path they tried plus `card=` and `dir=` when they fail, which
narrowed it in one attempt. That instrumentation is worth keeping.

The cause is one line in SdFat:

    inline bool lfnLegalChar(uint8_t c) {
      return !(lfnReservedChar(c) || c & 0X80);
    }

Every byte with the high bit set is illegal in a long file name, so the
*open* failed, not the write. SdFat can be built with
`USE_UTF8_LONG_NAMES`, but its own config warns it costs "significantly more
flash memory and a small amount of extra RAM" -- and RAM is the resource
this firmware has spent the most effort clawing back. It would also change
how files are named on a card shared with the reader firmwares.

So filenames fold to plain ASCII (`ascii_fold.h`): `ação` becomes `acao`.
The user's call, and the right one -- but the important property is that it
is a *rule*, not a truncation. `SAVE` and `LOAD` both apply it, so `LOAD
"ação"` opens the file `SAVE "ação"` created, and so does `LOAD "acao"`.

`asciiFold()` takes a codepoint rather than a byte because its two callers
hold different encodings, which is the kind of detail that becomes a bug if
left implicit: BASIC strings are Latin-1, so the byte *is* the codepoint,
while the editors' titles are UTF-8 and have to be decoded first.

That second caller was quietly broken in its own way, found while wiring
this up. `titleToFilename()` walked the title byte by byte and kept only
`a-z0-9`, so an accented letter -- two UTF-8 bytes, neither of them in that
set -- vanished entirely: naming a program `ação` in the editor produced
`aao.bas`. It decodes and folds now, so it produces `acao.bas`. Losing the
accent is expected; losing the letter was not.

## CLR leaves one byte behind (upstream, diagnosed)

`NEW` erases the program and works correctly. `CLR` -- this interpreter's
name for what MS-BASIC and MSX called `CLEAR`, clearing variables while
leaving the program -- does not.

Noticed on the device: `A=5 : CLR : PRINT A` shows 2, but `A=8 : CLR :
PRINT A` shows 8, unchanged. Not random, and not "sometimes fails". Sampling
it on the host harness gave:

    A=5   -> 2        A=100 -> 32
    A=8   -> 8        A=3.5 -> 2
    A=1   -> 0.5

Those are exactly the values you get by zeroing three of a float's four
bytes and keeping the one at the *highest address* -- which, little-endian
on the C3, is the most significant byte. Checked against `struct.pack`, all
five match to the bit.

The cause is one comparison in upstream's `clrvars()`:

    for (i = himem; i < memsize; i++) memwrite2(i, 0);
    himem = memsize;

The variable heap grows downward from `memsize`, so the topmost allocated
object's last byte sits at index `memsize` -- which `i < memsize` excludes.
Exactly one byte survives, at the top of the heap, which belongs to the
*first* variable allocated. Everything else clears correctly: a second
variable reads 0, and strings clear properly. `A=8` looks like it "wasn't
cleared" only because 8.0 is `41 00 00 00` and its other three bytes were
already zero.

Not fixed. Same standing decision as the `CONT` bug: upstream behaviour
stays upstream unless it blocks something. Recorded because the failure is
quiet and plausible-looking -- a variable that reads 2 after being cleared
does not announce itself as a bug -- and because whoever finds it next
should not have to re-derive it. The fix, if it is ever wanted, is `i <=
memsize`.

`CLEAR` was **not** added as an alias for `CLR`. It was asked for and then
withdrawn in the same message once this behaviour came to light, and the
reasoning is worth keeping: an alias would have made a command that does not
work reachable by a second, more familiar name. `NEW` does what was actually
wanted.

### Both fixed after all

The `CLR` bug is now patched and `CLEAR` added, at the user's direction --
overriding the "document, don't patch" default recorded above, on the
grounds that the fix is trivial and the patch set already exists for exactly
this kind of intervention. Two new scripts, `patches/tinybasic/05` and `06`.

The fix is one comparison, `<` to `<=`. Before changing it, the obvious
worry: does writing at index `memsize` run off the end of the array? It does
not, and the answer is also *why the bug exists*. `mem` is declared
`mem_t mem[MEMSIZE]` and `ballocmem()` returns `MEMSIZE - 1`, so
`memsize == MEMSIZE - 1` is the last valid index. Upstream's loop stops one
short of a byte that is real, addressable, and part of the heap.

The alias exploits how the lexer already works rather than adding machinery.
It scans `keyword[]` in order and returns the token at the matching index in
`tokens[]`, so two indices carrying the same token are simply two spellings.
Order matters for a reason worth recording: `LIST` and `HELP` print a token
by searching for its *first* index, so putting `CLR` ahead of `CLEAR` means a
line typed as `20 CLEAR` lists back as `20 CLR`. Verified on the harness --
listings stay in one spelling no matter which was typed.

Verified after patching, on the host harness before flashing: all five values
that used to survive (`5, 8, 100, 3.5, 1`) now clear to 0 under both
spellings, strings clear, the program survives `CLEAR`, and `FOR`/`NEXT`,
`LEN`, `NEW` and `LIST` are unaffected.

`CONT` stays documented rather than patched. That is not inconsistency: this
fix only touches bytes the function already meant to zero, while `CONT`
needs interpreter state saved and restored across a break.

## Invaders piled up in the right corner

Reported after the first hardware run. The cause is arithmetic, and the fix
came from reconstructing the screen rather than from staring at the program:
the harness emits VT52 that upstream's POSIX runtime converts to ANSI, so a
short Python script can replay the escape stream into a 20x64 grid and print
what the panel would have shown. The pile appeared at column 62, on the nose.

The fleet is drawn as a 31-character string at `OX-1`, with `OX` bounded to
34, so it reaches column 63. The band-clearing routine printed 60 spaces from
column 2 — columns 2 to 61. Columns 62 and 63 were never cleared, so every
time the fleet dropped from the right edge it left its rightmost invader
behind, one per descent, stacking into a column.

Now clears 1 to 63, which is exactly the span the fleet can occupy. Verified
by replaying the screen again: empty.

Worth noting the harness had to be rebuilt for this — the scratchpad does not
survive between sessions. It is reproducible in about a minute from
`../_backups/stefan-tinybasic.bundle` (the mirror `fetch.sh` mentions) plus
the patched headers already in `editor/lib/TinyBasic/`, and the screen
replayer is worth keeping the recipe for.

## Pacing: the verdict, and where the games should go

First hardware verdict on the real-time games: **tolerable, not good.** At
roughly one frame per second that is the honest answer, and no amount of
program-level cleverness changes it — the panel is the panel.

Which points at the genre rather than the implementation. The right target is
the ZX81-era puzzle game: push-the-crates, order-of-operations problems where
the whole challenge is *thinking* and a move takes as long as it takes. There
the second between frames is not latency, it is the pause while you look at
the board. `lander.bas` and `forca.bas` already work on that principle; the
next examples should lean the same way rather than trying to be arcade games
on a display that cannot be one.

### sokoban.bas: o gênero certo, e um bug que só a tela mostrou

Escrito como resposta direta ao veredito de ritmo. Não tem relógio: o laço
espera em `GET` até vir uma tecla, então o painel só repinta quando o jogador
decide alguma coisa. Não existe quadro perdido, e o segundo de refresh vira o
tempo de olhar o tabuleiro.

Duas coisas foram verificadas antes de escrever uma linha de BASIC, pela
mesma razão que o lander foi simulado: **um nível de Sokoban insolúvel não se
anuncia**. Um solver BFS em Python confirmou os quatro níveis (11, 17, 25 e
28 movimentos) e descartou dois candidatos impossíveis que pareciam bons no
papel. Depois o programa foi rodado com a solução do solver injetada no lugar
do `GET`, e a tela reconstruída a partir do fluxo de escapes.

Foi essa reconstrução que pegou o bug de verdade. A rotina que desenha uma
casa imprime `@` sempre que a casa é a do jogador, e o movimento repintava a
casa *velha* antes de atualizar `P` — então em vez de apagar o jogador ela o
redesenhava. O resultado era um rastro de `@` por todo lugar onde ele já
tinha passado, o tabuleiro inteiro sujo depois de vinte jogadas. Óbvio no
aparelho, invisível lendo o código, e teria custado uma viagem de ida e volta
ao hardware.

Vale o registro do método, porque agora se pagou três vezes (invaders,
sokoban, e o levantamento do `CLR`): **quando o sintoma é visual, reconstrua
a tela.** O harness emite VT52 que o runtime POSIX converte em ANSI; trinta
linhas de Python replicam isso numa grade e imprimem o que o painel mostraria.

## O RIGHT fantasma: resolvido, medindo

Resolvido no MicroWriter e trazido para cá — `InputManager` é a mesma
biblioteca nos dois. O levantamento completo está em
`../MicroWriter/docs/DEVELOPMENT_LOG.md`; o essencial:

Uma build temporária mostrou o valor cru do ADC na própria tela do menu, que é
a primeira coisa que aparece depois de uma troca de partição. Duas coisas
saíram dela.

**As duas primeiras amostras do laço devolvem `0` cravado**, nos dois caminhos
de reset, sem ninguém encostar no aparelho — idêntico em duas capturas, nos
mesmos ~4635 e ~4765ms. E `0` cai na faixa do RIGHT, que não tem piso
(`-INT32_MIN < leitura <= 750`). Daí o menu abrir já uma linha abaixo.

**Um RIGHT legítimo também lê `0`.** Isso derrubou a correção que eu ia
fazer: dar um piso à faixa teria desligado o botão direito. O RIGHT é a menor
resistência da escada e lê no fundo dela de verdade. Nenhuma regra sobre o
*valor* separa o fantasma do toque real, porque são o mesmo número — o
discriminador tem de ser **quando**, não **o quê**.

A guarda, então: em `InputManager::update()`, sobre o estado cru e antes do
debounce, nada é reportado enquanto a leitura não disser "nada pressionado" ao
menos uma vez. Um botão que nunca foi visto solto não pode ter sido apertado.

Três coisas que este caso deixou registradas, além da correção:

- Uma tentativa anterior falhou por olhar `isPressed()`, o estado **já
  debounced**, que atrasa e portanto reportava "nada pressionado" durante a
  própria falha. A ideia estava certa e o sinal, errado.
- Eu li um conjunto de números como se fosse do caminho vindo do CrossPoint
  quando era de um reset pós-flash, e com isso **descartei a hipótese certa**.
  Foi o usuário que corrigiu. Sem isso, teria ido procurar em outro lugar.
- Por que as duas primeiras conversões saem zeradas continua **em aberto**.
  Responder exigiria instrumentar o driver de ADC. A guarda não depende da
  resposta.

Também aqui: timeout de sleep de 5 para 15 minutos.

## O diagnóstico de arquivo estava gritando num caminho normal

Aparecendo no boot, acima do banner do interpretador:

    ?LOAD /MicroBASIC/programs/autoexec.bas
    ?card=1 dir=1

Ruído meu. O relatório de falha de arquivo foi acrescentado para caçar o
"File Error" do SAVE, e disparava também no arranque — porque o interpretador
procura um `autoexec.bas` com

    if (ifileopen("autoexec.bas")) { xload(...); st = SRUN; }

ou seja, **falhar ali é o caso normal**, não um defeito. Silenciado durante o
`basicSetup()` e religado logo depois, então um SAVE ou LOAD de verdade
continua explicando o que deu errado.

### E de brinde: existe autoexec

O trecho acima é uma funcionalidade que ninguém tinha notado que temos. Um
arquivo chamado `autoexec.bas` em `/MicroBASIC/programs` é carregado **e
executado** no boot (`st = SRUN`). É exatamente o que as máquinas da época
faziam, e transforma o aparelho em algo que liga já rodando um programa.
Nada precisou ser escrito para isso funcionar — só parar de reclamar da
ausência dele.

## O RIGHT físico volta ao editor de tela

Estava desativado ali como paliativo: era o botão que disparava sozinho e
corrompia a linha de programa sendo digitada, sem causa visível. A causa foi
encontrada e corrigida no `InputManager` (nada é reportado enquanto a leitura
crua não disser "nada pressionado" ao menos uma vez), então o paliativo saiu.

## autoexec: existia, mas não rodava

Descoberto ao silenciar o diagnóstico que reclamava da ausência dele. O
interpretador procura `autoexec.bas` no arranque, carrega e marca `st = SRUN`
-- mas quem executa isso é o `basicLoop()`, e a nossa ponte substitui o
`basicLoop()`. O arquivo carregava e ficava parado.

Quatro linhas em `tbSetup()`, as mesmas três instruções do upstream na mesma
ordem, e ele passa a rodar de verdade.

### E o d-pad precisou alcançar um programa em execução

Um lançador no boot é exatamente o momento em que nenhum teclado está pareado
ainda -- e até agora os botões físicos não chegavam a um programa rodando. A
fila de teclas é preenchida pelo BLE (que tem tarefa própria) e por
`processPhysicalButtons()`, que roda no `loop()` -- e o `loop()` está parado
dentro do interpretador durante todo o RUN.

`byield()` agora chama um `pumpPhysicalButtonsForProgram()`, na mesma cadência
do yield do escalonador (muito mais rápida que um dedo). Ele **não** é o
`processPhysicalButtons()`: aquele governa sleep, botão de power e transições
de estado da UI, e disparar isso de dentro do interpretador puxaria o chão do
programa em execução. Este só enfileira teclas de navegação.

Efeito colateral bem-vindo: o pacman e o sokoban passam a funcionar com o
d-pad, sem teclado.

### O lançador

`examples/autoexec.bas`. Menu de seis entradas, setas e ENTER. A primeira é
"SCREEN EDITOR" e apenas termina, caindo no prompt; as outras carregam um
programa.

Funciona porque `LOAD` **encadeia**: chamado de dentro de um programa em
execução ele zera o programa atual, carrega o novo e o roda
(`chain = 1; ... if (chain) st = SRUN;`). Não foi preciso inventar nada.

O que ele **não** pode fazer é chamar o `VC`, pela mesma razão que não pode
chamar `SCREEN`: os dois são comandos do firmware, interceptados antes do
interpretador, e portanto só existem digitados no prompt. Um programa BASIC
também não consegue listar o diretório -- o runtime tem `rootopen`/
`rootnextfile`, mas nada disso está exposto à linguagem. Por isso a lista é
fixa, em `DATA`. Para virar dinâmica seria preciso um statement novo no
interpretador, ou seja, um patch.

## A armadilha do autoexec, e as duas saídas que faltavam

Custou uma recuperação por cabo e esptool, e o erro foi inteiramente de
projeto meu.

Eu fiz o `autoexec.bas` rodar dentro do `setup()`. Um lançador é um programa
que roda **para sempre** — então, com um autoexec no cartão, o `loop()` nunca
começava. E é o `loop()` que lê o botão de power. Resultado: aparelho ligado,
sem menu, sem power, sem sleep, sem saída a não ser o reset — e no reset a
mesma coisa de novo.

Pior: eu já sabia da metade disso. Ao fazer o d-pad alcançar um programa em
execução, eu vi que o `loop()` fica parado durante o RUN e **tratei só a
navegação**, deixando o power de fora. O usuário topou com isso jogando
sokoban ("não lê o botão de power enquanto roda o basic") antes de eu ligar
os pontos.

Três correções, e todas são a mesma ideia aplicada onde faltava:

- **O power alcança um programa em execução.** Segurar 3s dorme, de dentro do
  interpretador. Seguro porque `enterDeepSleep()` não retorna.
- **O power só conta depois de ter sido visto solto uma vez.** Ligar o
  aparelho é segurar o power; sem isso o contador de 3s começava a correr com
  o dedo ainda no botão, e ligar mandava dormir. É literalmente a guarda que
  eu já tinha escrito para o d-pad e não apliquei aqui.
- **Segurar BACK no arranque pula o autoexec.** Lê o estado *cru* e exige que
  persista por ~300ms, porque a primeira conversão do ADC devolve 0 e uma
  leitura espúria não pode decidir isso sozinha.

E o Back físico agora interrompe qualquer programa (manda ESCAPE), fechando
um item que estava no log como limitação conhecida desde a integração.

A lição, e ela é sobre ordem e não sobre código: **eu pus uma ação
irreversível dentro de um caminho que roda no boot, e mandei testar um
autoexec antes de existir como sair dele.** A saída devia ter vindo primeiro.

Por decisão do usuário o aparelho fica **sem** autoexec; o lançador virou
`examples/menu.bas`, rodado à mão. O suporte no firmware fica, agora com as
saídas.

### Detalhes do arranque, validados no aparelho

- A ordem das linhas de boot inverteu: a identificação da máquina primeiro,
  o aviso de arranque abaixo dela. A mensagem passou a inglês, como o resto
  da interface: `Skipping autoexec.bas (BACK held)`. (O usuário sugeriu
  "Jumping", que em inglês seria pular por cima de algo físico; "skipping" é
  a palavra para omitir uma etapa.)

- **Segurar BACK só funciona se o botão já estiver segurado antes de ligar.**
  Tentar apertá-lo durante a limpeza de tela é tarde demais -- a amostragem
  acontece cedo no `setup()`, antes do primeiro desenho. Validado no
  aparelho e deixado assim: exigir o botão *antes* do power é o
  comportamento mais previsível, e é como as máquinas da época faziam.

- **Pulando o autoexec, o banner do interpretador continua não aparecendo.**
  Consequência direta de `autorun()`: ele retorna 1 assim que *encontra* o
  arquivo, e quem chama pula o `displaybanner()` -- a nossa decisão de não
  executar vem depois disso. Poderia ser contornado, e o usuário validou
  como está. Registrado porque a ausência do banner foi o sinal que
  identificou o autoexec como causa de toda a confusão da tarde; vale saber
  que ele significa "existe um autoexec no cartão", e não "algo falhou".

## Game Buttons: girar o d-pad para os programas

Item novo em Settings, entre "Font Size" e "Bluetooth":

    Game Buttons     As labelled | Rotated

Ligado, cada botao fisico manda a seta que ele realmente **aponta na tela**:

    DIREITA fisica -> cima          CIMA   fisica -> esquerda
    BAIXO   fisica -> direita       ESQUERDA fisica -> baixo

O mapa foi medido no aparelho pelo usuario, nao deduzido: o editor de tela
forca paisagem (LANDSCAPE_CCW) enquanto o d-pad esta fisicamente na lateral,
entao a rotacao entre um e outro nao se descobre lendo codigo.

Vale **so enquanto um programa BASIC executa** -- e o unico ponto por onde os
botoes alcancam um programa (`pumpPhysicalButtonsForProgram`). Menus, editores
e navegacao ficam intocados, por escolha explicita do usuario: ali a
consistencia da interface importa mais do que os botoes apontarem para o lado
certo depois de rotacionar a tela.

A ideia do ajuste em Settings foi do usuario e e melhor do que a minha, que
era fixar no codigo: a decisao fica com quem joga, e muda sem regravar
firmware.

## A data no vCodex: duas hipoteses minhas, as duas refutadas

Registrado porque o resultado util aqui foi negativo, e um negativo bem
estabelecido evita que alguem repita o caminho.

**Hipotese 1: "lemos a bateria no GPIO 0, que e o SCL do I2C".** Verdadeira
para o X3 e falsa para o X4. O firmware do leitor decide em tempo de
execucao:

    if (gpio.deviceIsX3()) { Wire.begin(X3_I2C_SDA, X3_I2C_SCL, ...); }
    else                   { pinMode(BAT_GPIO0, INPUT); }

No X4 ele le a bateria **do mesmo GPIO 0 que nos lemos**. Nao ha conflito, e
o medidor BQ27220 e o DS3231 sao hardware do X3.

Cheguei a trocar a leitura para I2C e gravar essa versao no aparelho antes de
verificar. Foi o usuario que mandou conferir como o leitor faz, e a
verificacao derrubou a mudanca. Revertida.

**Hipotese 2: "deixamos o pad em modo analogico e o leitor se identifica
errado como X3".** Improvavel: a deteccao roda duas passagens, exige 2 de 3
chips respondendo nas duas, e o resultado e **cacheado em NVS** -- a sondagem
nao se repete a cada boot.

Ou seja: **nao ha mecanismo conhecido** para a data ir a 26/08/2101 ao voltar
do MicroBASIC. O caminho honesto e medir, nao deduzir: o log do leitor imprime
`Using cached device type` e os escores da sondagem no arranque, e o NVS dele
guarda o tipo detectado e o estado do relogio. Capturar esse boot depois de
uma volta pelo MicroBASIC responde de uma vez se ele se acha um X3.

Nao mexer mais nesse pino sem essa evidencia.

### Os botoes do X4, fisicamente

Levantado pelo usuario, e vale registrado porque nao se descobre lendo
codigo -- os nomes no `InputManager` dizem a funcao, nao a posicao.

Olhando o aparelho em **portrait**:

- Abaixo da tela, duas barrinhas rocker. Cada uma e **dois** botoes sob uma
  mesma pecinha:
  - barrinha da esquerda: **BACK | ENTER**
  - barrinha da direita: **ESQUERDA | DIREITA**
- Na lateral direita, de cima para baixo: **POWER**, depois **CIMA** e
  **BAIXO**, e por fim **RESET**.

No editor de tela (LANDSCAPE_CCW) as duas barrinhas de baixo passam para a
direita da tela. E dai que vem a necessidade do "Game Buttons": os quatro
direcionais estao distribuidos entre duas barrinhas em orientacoes
diferentes, e nenhuma delas aponta para onde o nome do botao diz depois da
rotacao.

### O ajuste nao gravava

Reportado: mudar para "Rotated", sair, reiniciar, e encontrar "As labelled"
de volta -- e a rotacao nao funcionando no jogo.

Uma causa so para os dois sintomas. Eu acrescentei a *escrita* do valor no
bloco de persistencia mas nao o incluí na *condicao* que dispara esse bloco,
que compara cada ajuste com o seu "ultimo salvo". Mudando apenas este item,
nenhuma comparacao mudava e a gravacao nunca acontecia. E como o usuario
precisou reiniciar para carregar o Sokoban (esta sem teclado), o valor ja
tinha voltado a falso antes do jogo comecar -- por isso a rotacao "tambem"
nao funcionou. Nao eram dois defeitos.

Aproveitado para incluir o item no backup em `/MicroBASIC/ui_prefs.json`,
como os outros ajustes, para sobreviver a uma regravacao de firmware.

## A data no vCodex: mecanismo completo

Fechado, e o resultado corrige duas analises minhas anteriores. Vale por
inteiro porque o caminho ate aqui foi todo por eliminacao medida, e porque a
pergunta certa foi do usuario, nao minha.

### O que foi eliminado, com evidencia

**Colisao de NVS.** Dump da particao (0x9000, 20KB) antes e depois do
percurso MicroBASIC -> vCodex: **byte a byte identica**. Nada que este
firmware escreve toca o estado do leitor.

**Identificacao errada de hardware.** `cphw/dev_det = 1`, e no enum dele
`Unknown=0, X4=1, X3=2`. Ele sabe que e um X4 e tem isso cacheado. A
hipotese de ele se achar um X3 e ler um DS3231 inexistente morre aqui.

**Nosso uso do GPIO 0.** No X4 o proprio leitor le a bateria do mesmo pino
por ADC (`pinMode(BAT_GPIO0, INPUT)`); o barramento I2C e do X3. Cheguei a
trocar nossa leitura para I2C e gravar essa versao antes de verificar --
revertida depois que o usuario mandou conferir como o leitor faz.

### O mecanismo

Sem DS3231 (`HalClock::begin()` desiste na primeira linha se nao for X3), o
dia vive em `/.crosspoint/state.json` **no cartao SD**, campo
`lastKnownValidTimestamp`. E atualizado assim:

    lastKnownValidTimestamp =
        std::max(lastKnownValidTimestamp, getCurrentValidTimestamp());

e a validacao e apenas um piso:

    VALID_CLOCK_THRESHOLD = 1704067200   // 2024-01-01
    isClockValid(e) { return e >= VALID_CLOCK_THRESHOLD; }

So rejeita relogio pequeno demais. Nunca grande demais. Entao:

1. Na troca de particao o relogio de sistema volta com lixo. A referencia do
   ESP-IDF (`s_boot_time`) fica na memoria RTC, cujo layout o linker define
   **por binario** -- nos declaramos zero variaveis ali, o leitor declara
   onze (logs, panico, estagio de boot). Duas plantas diferentes do mesmo
   terreno.
2. O lixo observado foi 4154457600 = **26/08/2101**. Maior que 2024, passa.
3. O `max` o adota e **grava no cartao**.
4. Sendo `max`, nunca mais desce. So o "Set date" manual conserta, porque
   aquele caminho atribui (`registerValidTimeSync`) em vez de comparar.

Isso explica tudo o que o usuario havia observado e que eu nao sabia
encaixar: a data atravessar dias desligado (esta num arquivo, nao em memoria
volatil), nao se curar sozinha (o `max` e catraca), e morrer so na troca de
particao (mesmo binario, mesmo layout).

### A mitigacao, e por que ela nao custa nada

`switchToOtaApp()` zera o relogio de sistema antes do `esp_restart()`. Assim
`getCurrentValidTimestamp()` devolve 0, o `max` fica com o valor **do
arquivo**, e a data guardada e preservada.

Eu tinha proposto isso antes descrevendo o custo como "perde a sincronia
para nao ganhar data errada", e o usuario recusou -- com razao, dado o que eu
havia dito. Estava errado sobre o custo: **nao ha custo.** Sem o `max`
engolir lixo, o arquivo mantem o que ja tinha; o unico efeito e este boot nao
contar como sincronizado, que ja e o normal num X4 depois de qualquer evento
de energia.

Mitigacao e nao correcao: a causa e o layout da memoria RTC diferir entre
binarios, e disso nao ha como escapar de fora. O que da para fazer e nao
entregar um numero que a heuristica do outro lado aceite.

## Timestamp dos arquivos no SD (levantamento -- implementado depois, ver adiante)

Levantado depois de fechar a data do vCodex, e **deliberadamente nao
implementado** -- registrado aqui validado e pronto, para a decisao ser
tomada quando fizer sentido, sem redescobrir nada.

### O achado

Este firmware **nao registra callback de data no SdFat**. E o SdFat so grava
os campos de data da entrada de diretorio se houver um:

    // FatFile.cpp
    if (FsDateTime::callback) {
      FsDateTime::callback(&date, &time, &ms10);
      setLe16(dir->modifyDate, date);
      setLe16(dir->accessDate, date);
      setLe16(dir->modifyTime, time);
    }

Sem callback, `FsDateTime::callback` e `nullptr` e os campos ficam como
estavam -- zero num arquivo novo, que a FAT le como `1980-00-00`. E por isso
que os arquivos gravados aqui aparecem sem data ao abrir o cartao num PC.

Nao e efeito colateral da mitigacao do relogio: o nosso relogio ja era
invalido antes dela, porque chegando por troca de particao ele volta com lixo
de qualquer forma. O zeramento acontece na *saida*, e nao afeta o que
gravamos durante a sessao.

### Como o leitor resolve

`cpr-vcodex/src/util/SdFatDateTime.cpp` registra um callback e, quando o
relogio nao e valido, cai para o **ano de compilacao** em vez de zero:

    if (!TimeUtils::isClockValid(epoch)) {
      *date = FS_DATE(compileYear(), 1, 1);
      *time = FS_TIME(0, 0, 0);
    }
    ...
    void TimeUtils::registerSdFatDateTimeCallback() {
      FsDateTime::setCallback(sdFatDateTimeCallback);
    }

Tambem rejeita anos fora de 1980..2107, que e a faixa que a FAT representa.

### As tres opcoes, com o que cada uma custa

**1. Nao fazer nada.** Arquivos sem data. Ordenar por data no PC nao funciona;
o resto funciona. E o estado atual.

**2. Copiar o comportamento do leitor** -- callback com fallback para o ano de
compilacao. Barato (uma funcao, uma chamada no `setup()`), e da arquivos com
data plausivel e ordenavel. Mas como o relogio aqui **nunca** e sincronizado,
na pratica *todos* os arquivos ficariam com a data de compilacao do firmware.
Melhor que 1980, e ainda assim nao e a data real.

**3. Ler a data do proprio aparelho.** O leitor guarda
`lastKnownValidTimestamp` em `/.crosspoint/state.json`, no mesmo cartao (ver a
secao anterior). Da para ler esse campo no arranque e usar como base do
callback -- e dado do proprio dispositivo, nao invencao nossa, e seria a data
real dentro de um dia.

O custo e criar dependencia de um arquivo que nao e nosso, e a leitura tem de
ser escrita partindo do principio de que **ele pode simplesmente nao existir**:

- cartao novo, ou formatado;
- MicroBASIC usado sozinho, sem o leitor nunca ter rodado ali;
- o leitor presente mas nunca sincronizado, com o campo em 0;
- o arquivo existindo com esquema diferente numa versao futura dele.

Em todos esses casos o comportamento correto e o mesmo: **ignorar e seguir**,
caindo na opcao 2. Nada de erro na tela, nada de recusar gravar o arquivo --
uma data de arquivo e informacao acessoria, e falhar barulhentamente por
causa dela seria pior que a ausencia dela. Ler uma vez no arranque e guardar
o resultado; nao tentar de novo a cada arquivo gravado.

### Se um dia for implementar

- O callback e registrado uma vez, depois de `SdMan.begin()`.
- A assinatura e `void cb(uint16_t* date, uint16_t* time, uint8_t* ms10)`, e
  os macros `FS_DATE(y,m,d)` / `FS_TIME(h,m,s)` fazem o empacotamento.
- Validar 1980..2107 antes de escrever; fora disso a FAT nao representa.
- Vale conferir se o campo de criacao (`creationDate`) tambem interessa --
  o trecho acima so cobre modificacao e acesso.
- Indo pela opcao 3: checar existencia antes de abrir, tratar leitura vazia,
  JSON malformado e campo ausente como "sem data", e validar a faixa
  1980..2107 sobre o valor lido antes de confiar nele. Um `lastKnownValid`
  de 0 -- que e o valor inicial do leitor -- tem de cair no fallback, nao
  virar 1970.

### Implementado

Feito, em `editor/src/sd_datetime.{h,cpp}`, seguindo a opcao 3 com o
tratamento de ausencia que o usuario pediu. Registrado uma vez no arranque,
logo apos `fileManagerSetup()`.

A data vem de `lastKnownValidTimestamp` em `/.crosspoint/state.json`, e cai
para a data de compilacao quando o arquivo nao existe, nao abre, nao tem o
campo, tem o campo em 0, ou tem um valor implausivel. Nenhum desses e erro:
nao ha mensagem na tela nem recusa em gravar.

Tres decisoes que vale explicar, porque nenhuma e obvia:

**Varredura byte a byte em vez de carregar o arquivo.** O state.json do leitor
tem varios KB, e bloco contiguo de heap ja foi problema serio neste firmware.
Roda uma vez, entao a lentidao nao importa.

**Acumulacao propria em uint32 em vez do `jsonGetInt()` do projeto.** Aquele
usa `atoi()`, que devolve `int`; o epoch corrompido de 4154457600 estoura e
vira lixo com sinal. Lendo em uint32 ele chega inteiro e e recusado pela
faixa, que e o comportamento que se quer.

**Teto de plausibilidade, que o leitor nao tem.** A validacao dele so tem piso
(`>= 2024-01-01`), e foi exatamente isso que deixou o 2101 entrar e ser
gravado pelo `std::max`. Copiar a heuristica dele seria herdar o defeito: se
lessemos aquele arquivo depois da corrupcao, *todos* os nossos arquivos
ficariam em 2101. O teto e o ano de compilacao mais vinte -- uma data decadas
a frente do firmware que a esta lendo nao e uma data.

**Nao mexe no relogio de sistema.** Guarda a base numa variavel e soma
`millis()`. Escrever o relogio moveria a referencia na memoria RTC, que e a
origem de toda a confusao da data (secao anterior).

Duas verificacoes fora do aparelho pegaram defeitos antes de gravar: a
primeira versao de `compileDate()` passava a string inteira (`"Aug 20 2026"`)
para `strstr()` procurar na tabela de meses, nunca casava, e **todo arquivo
sairia em janeiro**; e sem o teto, o epoch de 2101 era aceito.

Testado somente contra o CPR-vCodex. CrossPoint e CrossInk nao foram
verificados e provavelmente nao mantem nada parecido -- por isso a ausencia
do arquivo e caso normal, e nao excecao.

### A hora fica 3h adiantada (conhecido, nao corrigido)

Testado no aparelho: a **data** sai certa, mas a **hora** vem adiantada pelo
fuso -- 19h47 gravado quando eram 16h47 locais, exatamente UTC-3.

A causa e uma linha: usamos `gmtime_r()`, que da UTC, e a FAT guarda **hora
local** por convencao. O leitor faz certo:

    TimeUtils::configureTimezone();          // setenv("TZ", ...); tzset();
    localtime_r(&currentTime, &localTime);

Corrigir exige o fuso, e e ai que a coisa para de ser barata. O ajuste do
leitor guarda apenas o **indice** do fuso (`timeZonePreset` em
`/.crosspoint/settings.json`), nunca a string POSIX. A tabela que traduz
indice em string vive so no binario dele
(`src/util/TimeZoneRegistry.cpp`, 30 entradas).

Ou seja, para ler o fuso dele terismos de **copiar a tabela** -- e herdar um
modo de falha desagradavel: se o leitor inserir ou reordenar um fuso, nossos
indices passam a significar outra coisa **em silencio**. Nada quebra, nada
avisa, os arquivos so passam a ter hora de outro continente. E o mesmo tipo de
acoplamento invisivel que ja nos custou caro nesta sessao.

Chegou a ser implementado e foi revertido por decisao do usuario. Fica assim:
**data certa, hora adiantada pelo fuso.** Para ordenar arquivos nao muda nada,
que era o objetivo original.

Se um dia valer, as opcoes sao: copiar a tabela aceitando o risco de deriva
(e conferir o tamanho dela ao portar -- se nao forem 30 entradas, ja
divergiu); ou um ajuste de fuso proprio nos Settings do MicroBASIC,
independente mas mais uma coisa para manter. Assumir UTC-3 fixo nao vale: e a
constante que funciona hoje e mente quando muda o horario de verao ou o
aparelho troca de pais.

## Veredito do hardware: as 12 fases e o Rotated

**As 12 fases do sokoban foram jogadas e concluidas no aparelho.** A rampa de
dificuldade, montada pela contagem de movimentos da solucao otima (10 a 32),
se sustentou na pratica -- nenhuma fase precisou ser reordenada. Vale como
confirmacao de que a aproximacao serve: contagem de movimentos nao mede
dificuldade percebida, mas para desenhar uma progressao ela bastou.

**O `Game Buttons` funciona ao longo de partidas inteiras**, nao so nas duas
telas em que foi testado a principio, e a interface normal segue com o
mapeamento de sempre.

E o veredito do usuario sobre ele vale mais que "funciona", porque delimita o
que faz sentido escrever daqui em diante:

> Para jogos de turnos ou aplicativos simples que precisam de pouca
> interacao a interface e funcional o suficiente, temos as 4 direcoes e ENTER.

Ou seja: **quatro direcoes e um confirmar**, sem teclado. E o orcamento de
entrada real do aparelho quando ele esta na rua, e ele combina exatamente com
o genero que o painel ja pedia -- turnos, quebra-cabecas, menus. Um programa
que precise de mais teclas que isso so funciona em casa, com o teclado BLE
pareado, e vale saber disso antes de escrever e nao depois.

### Confirmado no aparelho (desfazer, .bak, data)

Gravado e testado no MicroBASIC: os cinco casos do desfazer, o Ctrl+L, a
ausencia de `.bak` ao lado dos arquivos, e a data.

A data merece nota, porque aqui a expectativa e outra: o cartao deste
aparelho tem o CPR-vCodex, entao o `lastKnownValidTimestamp` existe e a data
sai **real**, avancando dentro da sessao pelo `millis()` -- ao contrario do
aparelho do MicroWriter, cujo cartao tem CrossPoint, que usa o mesmo arquivo
mas nao guarda o campo, e por isso cai na data de compilacao fixa. Os dois
caminhos do codigo ficaram exercitados em hardware, cada um no seu aparelho.

## SCREEN 2 e 3 ilegiveis: empacotamento de bits, nao a fonte

Reportado como "as fontes em SCREEN 2 e 3 estao completamente ilegiveis,
apesar de as previas geradas serem legiveis". O diagnostico veio do port para
o M5PaperS3, com foto do hardware mostrando texto de interface nitido ao lado
da grade do terminal borrada **no mesmo quadro** -- o que ja apontava para o
caminho da fonte da grade, nao para o renderizador.

**A causa:** `emit_epdfont_header.py` empacotava cada linha do glifo
separadamente, arredondando para `ceil(width/8)` bytes por linha. O
`renderCharImpl()` do GfxRenderer le o oposto -- um fluxo continuo pelo glifo
inteiro:

    const int pixelPosition = glyphY * width + glyphX;
    const uint8_t byte = bitmap[pixelPosition >> 3];

Para larguras multiplas de 8 os dois esquemas **coincidem por acaso**, e e por
isso que ninguem notou em anos: SCREEN 0 (24) e SCREEN 1 (16) sempre
renderizaram certo. SCREEN 2 (12) e SCREEN 3 (10) desalinham 4 e 6 bits por
linha, acumulando -- da segunda linha em diante o glifo vira ruido. Esses dois
modos **nunca** renderizaram corretamente.

Corrigido substituindo o empacotamento linha a linha por um fluxo continuo,
com preenchimento so no fim do ultimo byte. Confirmacoes de que a correcao e
exatamente essa:

- `unscii_24x48.h` e `unscii_16x32.h` sairam **byte a byte identicos** aos
  anteriores, como a teoria previa.
- O tamanho dos outros dois caiu para o exato: 12x24 -> 36 bytes/glifo,
  10x20 -> 25. O que sobrava antes era padding de linha.
- Decodificando o `.h` com o mesmo algoritmo do `renderCharImpl`, os glifos
  saem como formas legiveis; antes saiam como ruido a partir da linha 2.

### O que eu tinha diagnosticado errado

Antes de receber esse achado, eu havia decodificado os `.h` com o esquema
**linha a linha** -- o mesmo do gerador -- e portanto vi os glifos como o
gerador os pretendia, nao como o aparelho os lia. Vi tracos de 3px e conclui
que o problema era a ampliacao fracionaria (1.25x e 1.5x de unscii 8x16), e ia
propor trocar por Terminus, que temos em 10x20 e 12x24 **nativos** em
`research/fonts/src/`.

Estava olhando o lugar certo pelo motivo errado: decodifiquei com o mesmo
engano do codigo que gerou o arquivo, entao os dois erros se cancelaram e o
defeito ficou invisivel. A licao e concreta: **ao verificar um formato,
decodifique com o consumidor real, nao com o produtor.**

### O que fica em aberto (opcional)

A ampliacao fracionaria continua existindo: em 10x20, unscii sai com tracos de
3px, contra 1px do Terminus nativo. Agora que o empacotamento esta certo, da
para julgar isso no painel de verdade -- pode ser que 3px leia melhor em
e-ink, onde traco de 1px as vezes fica palido. Se nao ler, os arquivos para a
troca ja estao no repositorio.
