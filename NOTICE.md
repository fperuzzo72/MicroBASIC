# Third-party notices

`research/fonts/` carries font source files pulled in for the SCREEN 0/1/2
text-mode font evaluation (see README.md and `research/fonts/previews/` for
the rendered 800x480 1-bit comparison sheets). Each family's own license file
is preserved alongside it in `research/fonts/src/`.

## Spleen

BDF files `spleen-8x16.bdf`, `spleen-12x24.bdf`, `spleen-16x32.bdf`, from
[fcambus/spleen](https://github.com/fcambus/spleen) by Frédéric Cambus.
BSD 2-Clause license — full text in `research/fonts/src/spleen-LICENSE`.

## Terminus Font

BDF files `ter-u16n.bdf`, `ter-u24n.bdf`, `ter-u32n.bdf`, from
[Terminus Font](https://terminus-font.sourceforge.net/) by Dimitar Zhekov
(mirrored at
[balabit-deps/balabit-os-8-xfonts-terminus](https://github.com/balabit-deps/balabit-os-8-xfonts-terminus)).
SIL Open Font License 1.1 — full text in
`research/fonts/src/terminus-LICENSE`.

## Tamzen

BDF files `Tamzen6x12r.bdf`, `Tamzen8x16r.bdf`, `Tamzen10x20r.bdf`, from
[sunaku/tamzen-font](https://github.com/sunaku/tamzen-font), a fork of
Scott Fial's [Tamsyn](http://www.fial.com/~scott/tamsyn-font/) font. Free to
use, copy, and modify — full text in `research/fonts/src/tamzen-LICENSE`.

## Unscii

Hex files `unscii-8.hex`, `unscii-16.hex`, from
[viznut/unscii](https://github.com/viznut/unscii) by Ville-Matias Heikkilä
(viznut). Public domain / CC0 — full text in
`research/fonts/src/unscii-LICENSE`. The `Unscii_large_16x32.bmp` preview is
a 2x nearest-neighbor pixel-double of `unscii-16.hex` (no native 16x32
variant exists).

## Ultimate Oldschool PC Font Pack (IBM CGA/EGA/VGA)

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

## MSX ROM font (HotBit) — rendered preview only, raw data not included

`research/fonts/previews/HotBit-MSX1_font_8x8.bmp` was rendered from the
character generator table of a `hotbit13p.rom` (32KB MSX1 BIOS+BASIC,
HB-8000-class Brazilian HotBit clone) dumped by the project owner from
their own hardware. Included here only as a personal visual-comparison
reference alongside the openly-licensed fonts above.

The **raw extracted font data** (the 2048-byte glyph table pulled out of
the ROM) is deliberately *not* committed anywhere in this repo and not
redistributed — the MSX character ROM itself is copyrighted
(Microsoft/ASCII Corporation lineage), unlike every other font in this
file. Kept local only, on the project owner's own machine.
