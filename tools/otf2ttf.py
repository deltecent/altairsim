#!/usr/bin/env python3
#
# Convert a CFF OpenType (.otf) font to TrueType (.ttf), deterministically.
#
#   tools/otf2ttf.py IN.otf OUT.ttf
#
# WHY THIS EXISTS. The PDFs are printed by Chrome headless --print-to-pdf
# (tools/build-docs.sh). Chrome embeds a CFF .otf as a Type 3 font, whose text layer has no
# reliable word spacing -- so copy-paste out of the PDF garbles ("Using alt airsim, ...";
# issue #246). A TrueType (glyf) font embeds as CID TrueType and copies clean, which is why
# the code font (DejaVu, already .ttf) was never affected and the body font (XCharter, .otf)
# was every word. XCharter ships no .ttf upstream, so docs/fonts/XCharter-*.ttf is produced
# from the upstream .otf by THIS script. See docs/fonts/README.md for the hashes on both ends.
#
# THIS IS NOT PART OF THE BUILD, and, like build-docs.sh, must never become part of it: it
# was run once to make the committed .ttf files and is kept only so that conversion is
# reproducible and auditable. cu2qu is deterministic and fontTools copies the source `head`
# timestamps verbatim, so a given fontTools on a given .otf yields byte-identical output.
# Recorded run: fontTools 4.60.2, MAX_ERR = 1.0 (cu2qu's default em-unit tolerance).
import sys
from fontTools.ttLib import TTFont, newTable
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

MAX_ERR = 1.0        # cu2qu approximation tolerance, in em units
POST_FORMAT = 2.0    # keep glyph names (format 2), matching the source

def glyphs_to_quadratic(glyphs, max_err):
    quad = {}
    for name, glyph in glyphs.items():
        pen = TTGlyphPen(glyphs)
        glyph.draw(Cu2QuPen(pen, max_err))
        quad[name] = pen.glyph()
    return quad

def main(src, dst):
    font = TTFont(src)
    assert "CFF " in font, f"{src} is not a CFF OpenType font"
    glyph_order = font.getGlyphOrder()
    glyph_set = font.getGlyphSet()
    quad = glyphs_to_quadratic(glyph_set, MAX_ERR)

    del font["CFF "]
    font.sfntVersion = "\x00\x01\x00\x00"   # TrueType signature; was 'OTTO' (CFF)
    glyf = font["glyf"] = newTable("glyf")
    glyf.glyphOrder = glyph_order
    glyf.glyphs = quad
    font["loca"] = newTable("loca")
    if "VORG" in font:
        del font["VORG"]

    glyf.compile(font)          # populates maxPoints/maxContours/maxComponent* below
    maxp = font["maxp"]
    maxp.tableVersion = 0x00010000
    # maxp 1.0 carries hinting limits a CFF font never had; this TTF has no hinting
    # program, so they are all zero. maxZones is 1 (a font with no twilight zone).
    maxp.maxZones = 1
    maxp.maxTwilightPoints = 0
    maxp.maxStorage = 0
    maxp.maxFunctionDefs = 0
    maxp.maxInstructionDefs = 0
    maxp.maxStackElements = 0
    maxp.maxSizeOfInstructions = 0
    post = font["post"]
    post.formatType = POST_FORMAT
    post.extraNames = []
    post.mapping = {}
    post.glyphOrder = glyph_order

    font.recalcBBoxes = True
    font.save(dst)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: otf2ttf.py IN.otf OUT.ttf")
    main(sys.argv[1], sys.argv[2])
