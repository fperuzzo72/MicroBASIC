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
