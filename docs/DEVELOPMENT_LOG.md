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
