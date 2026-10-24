"""Build the Stock-style numeral font from TeX Gyre Heros Bold Italic.

    python tools/make_stock_font.py <texgyreheros-bolditalic.otf> <out.ttf> [angle_deg]

TeX Gyre Heros is a free Helvetica clone (GUST Font License), the closest free
match to the factory NB1 cluster numerals, including Helvetica's long-flag "1".
This script:
  1. keeps only basic Latin (0x20-0x7E) and the degree sign,
  2. converts the CFF outlines to TrueType (LVGL's TinyTTF reads those most reliably),
  3. adds slant so the italic angle becomes `angle_deg` (default 16; the source is about 12),
     shearing about half the cap height so each glyph's ink stays centred in its advance,
  4. renames the font, which the GUST licence requires for modified versions.
"""
import math
import sys

from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable

FAMILY = "NB1 Stock Numerals"
PS_NAME = "NB1StockNumerals-BoldItalic"


def main(src, dst, target_deg=16.0):
    font = TTFont(src)
    assert font.sfntVersion == "OTTO" and "CFF " in font, "expected a CFF OpenType font"

    # 1. subset
    opts = subset.Options()
    opts.layout_features = []          # TinyTTF ignores GSUB/GPOS
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=list(range(0x20, 0x7F)) + [0xB0])
    sub.subset(font)

    src_angle = abs(font["post"].italicAngle) or 12.0
    extra = math.radians(max(0.0, target_deg - src_angle))
    t = math.tan(extra)
    cap = getattr(font["OS/2"], "sCapHeight", 0) or 720
    shear = (1, 0, t, 1, -t * cap / 2, 0)   # x' = x + t*y - t*cap/2

    # 2 + 3. CFF -> quadratic glyf, sheared on the way
    glyph_set = font.getGlyphSet()
    order = font.getGlyphOrder()
    quad = {}
    for name in order:
        tt = TTGlyphPen(None)
        glyph_set[name].draw(TransformPen(Cu2QuPen(tt, 1.0, reverse_direction=True), shear))
        quad[name] = tt.glyph()
    font["loca"] = newTable("loca")
    glyf = font["glyf"] = newTable("glyf")
    glyf.glyphOrder = order
    glyf.glyphs = quad
    del font["CFF "]
    for tag in ("VORG", "GSUB", "GPOS", "GDEF"):
        if tag in font:
            del font[tag]
    glyf.compile(font)
    hmtx = font["hmtx"]
    for name, g in glyf.glyphs.items():
        g.recalcBounds(glyf)
        hmtx[name] = (hmtx[name][0], getattr(g, "xMin", 0))
    maxp = font["maxp"] = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for k in ("maxZones", "maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs",
              "maxStackElements", "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, k, 1 if k == "maxZones" else 0)
    post = font["post"]
    post.formatType = 2.0
    post.extraNames, post.mapping, post.glyphOrder = [], {}, order
    post.italicAngle = -target_deg
    font["hhea"].caretSlopeRise, font["hhea"].caretSlopeRun = 1000, int(round(1000 * math.tan(math.radians(target_deg))))
    font["head"].glyphDataFormat = 0
    font.sfntVersion = "\x00\x01\x00\x00"

    # 4. rename (GUST Font License: a modified font must not keep the original name)
    names = font["name"]
    for rec in list(names.names):
        if rec.nameID in (1, 3, 4, 6, 16, 17, 21, 22):
            names.removeNames(nameID=rec.nameID)
    for nid, val in ((1, FAMILY), (2, "Bold Italic"), (3, PS_NAME + "-nb1"), (4, FAMILY + " Bold Italic"), (6, PS_NAME)):
        names.setName(val, nid, 3, 1, 0x409)
        names.setName(val, nid, 1, 0, 0)
    names.setName("Derived from TeX Gyre Heros Bold Italic (GUST Font License): subset to basic Latin, "
                  "converted to TrueType outlines, slant increased to %g degrees for the NB1 gauge." % target_deg,
                  10, 3, 1, 0x409)

    font.save(dst)
    print("wrote %s: %d glyphs, slant %.1f -> %.1f degrees" % (dst, len(order), src_angle, target_deg))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 16.0)
