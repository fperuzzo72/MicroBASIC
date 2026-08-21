#!/usr/bin/env python3
"""Emits EpdFontData C headers for all four SCREEN 0/1/2/3 Unscii sizes,
bypassing fontconvert.py entirely (it only accepts FreeType-loadable font
files -- TTF/OTF/BDF -- not raw pixel arrays like the ones this project's
own Unscii resizer produces).

Reuses generate_screen_fonts.py's actual glyph pipeline -- ScaledHexFont
(clean integer 2x/3x block duplication) for the 48-col/32-col sizes,
UnsciiScreenFont (area-coverage resize + stem-width cap + the ç/Ç fix) for
the 80-col/64-col sizes -- so the firmware fonts are pixel-identical to
the already-validated preview BMPs in research/fonts/previews/, not a
second, potentially-diverging implementation.

Format matches editor/lib/EpdFont/builtinFonts/*.h exactly (verified
against EpdFontData.h and the pixel-placement math in GfxRenderer.cpp's
drawText()/renderChar()). Two steps, not one -- drawText() itself adds
the font's ascender to y *before* renderChar ever sees it:

    yPos    = y + fontData.ascender          // GfxRenderer::drawText()
    screenX = x    + glyph.left + glyphX     // GfxRenderer::renderChar()
    screenY = yPos - glyph.top  + glyphY

Every glyph here is the *entire* fixed-size cell (true monospace, not
proportionally trimmed like the project's prose fonts), so every glyph
gets identical left=0, top=0, width=cell_w, height=cell_h, advanceX=cell_w.
For `y` in drawText(fontId, x, y, ...) to land on the pixel row of the
*top* of the cell (no baseline-offset math needed by callers), the two
additions above have to cancel: ascender must be 0, matching top=0 -- NOT
cell_h, which is what an earlier version of this script emitted, and
which pushed every drawn character down by exactly one full cell height
(the cursor, drawn with fillRect() directly, doesn't go through
drawText() at all, so it stayed put -- that mismatch was the tell). 1-bit
packing (not the 2-bit grayscale mode the prose fonts use), MSB-first,
ceil(width/8) bytes per row.

Usage:
    python3 emit_epdfont_header.py
writes all four headers directly into
../../../editor/lib/EpdFont/builtinFonts/.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from generate_screen_fonts import ScaledHexFont, UnsciiScreenFont  # noqa: E402

SRC = os.path.join(os.path.dirname(__file__), "..", "src")
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "..", "..",
                        "editor", "lib", "EpdFont", "builtinFonts")


def pack_bits_contiguous(flat_bits):
    """flat_bits: valores 0/1 em row-major do glifo INTEIRO (width*height),
    empacotados como um unico fluxo continuo de bits, MSB primeiro -- NAO
    alinhado por linha.

    Tem de casar com renderCharImpl() do GfxRenderer.cpp, que le assim:

        const int pixelPosition = glyphY * width + glyphX;
        const uint8_t byte = bitmap[pixelPosition >> 3];

    ou seja, uma posicao que corre pelo glifo inteiro e nunca reinicia na
    fronteira de linha. Preenchimento com zeros so no fim do ultimo byte.

    A versao anterior empacotava linha a linha, arredondando para
    ceil(width/8) bytes por linha. Para larguras multiplas de 8 os dois
    esquemas coincidem por acaso -- e por isso SCREEN 0 (24) e SCREEN 1 (16)
    sempre estiveram corretos e ninguem notou. Para SCREEN 2 (12) e SCREEN 3
    (10) a leitura desalinha 4 e 6 bits por linha, acumulando: da segunda
    linha em diante o glifo vira ruido, e e por isso que esses dois modos
    estavam ilegiveis no painel.

    Encontrado no port para o M5PaperS3, com foto do hardware mostrando texto
    de interface nitido ao lado da grade do terminal borrada no mesmo quadro.
    """
    nbytes = (len(flat_bits) + 7) // 8
    value = 0
    for b in flat_bits:
        value = (value << 1) | (1 if b else 0)
    value <<= nbytes * 8 - len(flat_bits)
    return [(value >> (8 * (nbytes - 1 - i))) & 0xFF for i in range(nbytes)]


def build_intervals(codepoints):
    codepoints = sorted(codepoints)
    intervals = []
    start = prev = codepoints[0]
    for cp in codepoints[1:]:
        if cp == prev + 1:
            prev = cp
            continue
        intervals.append((start, prev))
        start = prev = cp
    intervals.append((start, prev))
    return intervals


def emit_header(font, cell_w, cell_h, name, source_note):
    codepoints = sorted(
        cp for cp in font.glyphs if (0x20 <= cp <= 0x7E) or (0xA0 <= cp <= 0xFF)
    )
    intervals = build_intervals(codepoints)
    for first, last in intervals:
        for cp in range(first, last + 1):
            assert cp in font.glyphs, f"{name}: gap at U+{cp:04X}"

    bitmap_bytes = bytearray()
    glyph_entries = []  # (width, height, advanceX, left, top, dataLength, dataOffset)
    for cp in codepoints:
        bits = font.get_cell_bits(chr(cp))
        glyph_offset = len(bitmap_bytes)
        flat_bits = [b for row in bits for b in row]
        bitmap_bytes.extend(pack_bits_contiguous(flat_bits))
        data_length = len(bitmap_bytes) - glyph_offset
        glyph_entries.append((cell_w, cell_h, cell_w, 0, 0, data_length, glyph_offset))

    out = []
    out.append("// Generated by research/fonts/tools/emit_epdfont_header.py -- do not hand-edit.")
    out.append(f"// {source_note}")
    out.append("// Public domain / CC0 (Unscii, viznut) -- see research/fonts/src/unscii-LICENSE.")
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append('#include "EpdFontData.h"')
    out.append("")

    out.append(f"static const uint8_t {name}Bitmaps[{len(bitmap_bytes)}] = {{")
    for i in range(0, len(bitmap_bytes), 16):
        chunk = bitmap_bytes[i:i + 16]
        out.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    out.append("};")
    out.append("")

    out.append(f"static const EpdGlyph {name}Glyphs[{len(glyph_entries)}] = {{")
    for w, h, adv, left, top, dlen, doff in glyph_entries:
        out.append(f"    {{ {w}, {h}, {adv}, {left}, {top}, {dlen}, {doff} }},")
    out.append("};")
    out.append("")

    out.append(f"static const EpdUnicodeInterval {name}Intervals[{len(intervals)}] = {{")
    running_offset = 0
    for first, last in intervals:
        out.append(f"    {{ 0x{first:X}, 0x{last:X}, {running_offset} }},")
        running_offset += last - first + 1
    out.append("};")
    out.append("")

    out.append(f"static const EpdFontData {name} = {{")
    out.append(f"    {name}Bitmaps,")
    out.append(f"    {name}Glyphs,")
    out.append(f"    {name}Intervals,")
    out.append(f"    {len(intervals)},")
    out.append(f"    {cell_h},  // advanceY")
    out.append("    0,  // ascender -- MUST be 0 to match glyph.top=0, see module docstring")
    out.append("    0,   // descender")
    out.append("    false,  // is2Bit")
    out.append("};")

    return "\n".join(out) + "\n", len(codepoints), len(intervals), len(bitmap_bytes)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    u16 = os.path.join(SRC, "unscii-16.hex")

    jobs = [
        ("unscii_24x48", ScaledHexFont(u16, 8, 16, 3), 24, 48,
         "SCREEN 0 (32-col): clean 3x nearest-neighbor scale of unscii-16.hex."),
        ("unscii_16x32", ScaledHexFont(u16, 8, 16, 2), 16, 32,
         "SCREEN 1 (48-col, default): clean 2x nearest-neighbor scale of unscii-16.hex."),
        ("unscii_12x24", UnsciiScreenFont(u16, 8, 16, 12, 24), 12, 24,
         "SCREEN 2 (64-col): area-coverage resize + stem-width cap + cedilla fix."),
        ("unscii_10x20", UnsciiScreenFont(u16, 8, 16, 10, 20), 10, 20,
         "SCREEN 3 (80-col): area-coverage resize + stem-width cap + cedilla fix."),
    ]

    for name, font, cell_w, cell_h, note in jobs:
        text, n_glyphs, n_intervals, n_bytes = emit_header(font, cell_w, cell_h, name, note)
        out_path = os.path.join(OUT_DIR, f"{name}.h")
        with open(out_path, "w") as f:
            f.write(text)
        print(f"wrote {name}.h: {n_glyphs} glyphs, {n_intervals} interval(s), {n_bytes} bitmap bytes")


if __name__ == "__main__":
    main()
