# Third-party notices

`research/fonts/` carries font source files pulled in for the SCREEN 0/1/2
text-mode font evaluation (see README.md and `research/fonts/previews/` for
the rendered 480x800 1-bit comparison sheets, portrait-rotated to match how
CPR-vCodex actually displays them). Each family's own license file is
preserved alongside it in `research/fonts/src/`.

## Spleen

BDF file `spleen-16x32.bdf` (the only size still in `previews/`, as
"medium") from [fcambus/spleen](https://github.com/fcambus/spleen) by
Frédéric Cambus. BSD 2-Clause license — full text in
`research/fonts/src/spleen-LICENSE`. `spleen-8x16.bdf` and
`spleen-32x64.bdf` are still in `src/` but no longer rendered — 8x16 is
below the 10x20 readability floor, 32x64 was confirmed larger than
worth the lost columns.

## Terminus Font

BDF files `ter-u20n.bdf`, `ter-u24n.bdf`, `ter-u32n.bdf` (regular) and
`ter-u20b.bdf`, `ter-u24b.bdf`, `ter-u32b.bdf` (bold, added to compare
against Tamzen Bold and regular Terminus at identical cell sizes —
Terminus Bold has a native size at every one of small/medium/large, no
scaling needed), from
[Terminus Font](https://terminus-font.sourceforge.net/) by Dimitar Zhekov
(mirrored at
[balabit-deps/balabit-os-8-xfonts-terminus](https://github.com/balabit-deps/balabit-os-8-xfonts-terminus)).
SIL Open Font License 1.1 — full text in
`research/fonts/src/terminus-LICENSE`. (`ter-u16n.bdf`, previously used,
stays in `src/` but dropped from previews — below the floor.)
`TerminusBold_xlarge_24x48.bmp` is a clean 2x nearest-neighbor upscale
of the bold 12x24 glyphs (`ter-u24b.bdf`), reaching toward the
~32-column range no native Terminus Bold size lands on exactly.

## Tamzen

Bold weight only: `Tamzen10x20b.bdf`, from
[sunaku/tamzen-font](https://github.com/sunaku/tamzen-font), a fork of
Scott Fial's [Tamsyn](http://www.fial.com/~scott/tamsyn-font/) font. Free to
use, copy, and modify — full text in `research/fonts/src/tamzen-LICENSE`.
The regular weight (`Tamzen6x12r.bdf`, `Tamzen8x16r.bdf`,
`Tamzen10x20r.bdf`, still in `src/`) read too thin/light on the e-ink
panel. The 2x/3x upscaled bold variants tried in the previous pass
(20x40, 30x60) were dropped this round — confirmed larger than worth
the lost columns; only the native 10x20 remains in `previews/`.

## Unscii

Hex files `unscii-8.hex`, `unscii-16.hex`, from
[viznut/unscii](https://github.com/viznut/unscii) by Ville-Matias Heikkilä
(viznut). Public domain / CC0 — full text in
`research/fonts/src/unscii-LICENSE`. No native Unscii size reaches the
10x20 floor (max is 8x16), so both remaining previews are nearest-neighbor
upscales of `unscii-16.hex`: `Unscii_small_16x32.bmp` (2x),
`Unscii_medium_24x48.bmp` (3x). The 4x upscale (32x64, previously
"large") was dropped this round — confirmed larger than worth the lost
columns.

## Ultimate Oldschool PC Font Pack (IBM CGA/EGA/VGA) — not currently in `previews/`

TTF files `Px437_IBM_CGA.ttf`, `Px437_IBM_EGA_8x14.ttf`,
`Px437_IBM_VGA_8x16.ttf` — hardware-authentic recreations of real IBM
adapter ROM fonts, by VileR, from
[The Ultimate Oldschool PC Font Pack](https://int10h.org/oldschool-pc-fonts/)
(mirrored at
[retro-vault/font-vault](https://github.com/retro-vault/font-vault)).
Creative Commons Attribution-ShareAlike 4.0 International
([full license text](https://creativecommons.org/licenses/by-sa/4.0/legalcode)) —
any redistribution of these three files, or of images/fonts derived from
them, must carry the same CC BY-SA 4.0 attribution and share-alike terms.
Weren't part of the round-2 feedback, so left out of this round's
previews; still in `src/` in case they come back into consideration.

## MSX ROM font (HotBit) — not currently in `previews/`

A preview was rendered from the character generator table of a
`hotbit13p.rom` (32KB MSX1 BIOS+BASIC, HB-8000-class Brazilian HotBit
clone) dumped by the project owner from their own hardware, for personal
visual-comparison reference alongside the openly-licensed fonts above.
It was pulled from `previews/` in the 10x20-floor resizing pass (its
native cell is a square 8x8, so the 2:1 width:height scaling used for
the other families doesn't map onto it directly) — can be re-added once
its own scaling is worked out.

The **raw extracted font data** (the 2048-byte glyph table pulled out of
the ROM) has never been committed to this repo and won't be — the MSX
character ROM itself is copyrighted (Microsoft/ASCII Corporation
lineage), unlike every other font in this file. Kept local only, on the
project owner's own machine.
