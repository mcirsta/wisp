#!/usr/bin/env python3
"""Regenerate the tiny Qt weight-test fonts (requires FontTools, not at build time).

Original rectangular glyphs, licensed under GPL-2.0-only like the tests. All
printable ASCII maps to one glyph; advances identify the selected weight.
"""
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables.TupleVariation import TupleVariation

DESTINATION = Path(__file__).resolve().parent


def make_font(family, style, weight, italic=False, width=5):
    builder = FontBuilder(1000, isTTF=True)
    order = [".notdef", "space", "box"]
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({code: "space" if code == 32 else "box" for code in range(32, 127)})
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(None)
        if name != "space":
            pen.moveTo((50, 0))
            pen.lineTo((50, 700))
            pen.lineTo((200 + weight // 4, 700))
            pen.lineTo((200 + weight // 4, 0))
            pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({name: (250 if name == "space" else 400 + weight // 2, 50)
                                    for name in order})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    ps_name = (family + "-" + style).replace(" ", "")
    builder.setupNameTable({"familyName": family, "styleName": style,
                           "typographicFamily": family, "typographicSubfamily": style,
                           "uniqueFontIdentifier": ps_name, "fullName": family + " " + style,
                           "psName": ps_name, "version": "Version 1.0"})
    flags = (1 if italic else 0) | (32 if weight >= 700 else 0)
    if weight == 400 and not italic:
        flags |= 64
    builder.setupOS2(usWeightClass=weight, usWidthClass=width, fsSelection=flags,
                     sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    builder.setupPost(italicAngle=-12 if italic else 0)
    builder.font["head"].macStyle = (2 if italic else 0) | (1 if weight >= 700 else 0)
    builder.font["head"].created = builder.font["head"].modified = 3406620153
    builder.font.recalcTimestamp = False
    return builder


for weight, style in [(100, "Thin"), (300, "Light"), (500, "Medium"),
                      (600, "SemiBold"), (700, "Bold"), (900, "Black")]:
    make_font("Wisp Weight Test", style, weight).save(DESTINATION / f"weight-{weight}.ttf")
for weight, style in [(500, "Medium Italic"), (700, "Bold Italic")]:
    make_font("Wisp Weight Test", style, weight, italic=True).save(DESTINATION / f"italic-{weight}.ttf")
make_font("Wisp Weight Test", "Condensed Black", 900, width=3).save(DESTINATION / "condensed-900.ttf")
for weight, style in [(400, "Regular"), (700, "Bold")]:
    make_font("Wisp Normal Test", style, weight).save(DESTINATION / f"normal-{weight}.ttf")

# Different vendor IDs make Qt expose two foundries for this one family.
for italic, vendor in [(False, "UKWN"), (True, "URW ")]:
    style = "Italic" if italic else "Regular"
    foundry = make_font("Wisp Foundry Test", style, 400, italic=italic)
    foundry.font["OS/2"].achVendID = vendor
    foundry.save(DESTINATION / f"foundry-{style.lower()}.ttf")

variable = make_font("Wisp Variable Test", "Regular", 400)
variable.setupFvar([("wght", 100, 400, 900, "Weight")],
                   [{"location": {"wght": weight}, "stylename": name}
                    for weight, name in [(400, "Regular"), (700, "Bold")]])
delta = TupleVariation({"wght": (0, 1, 1)}, [(0, 0), (0, 0), (100, 0), (100, 0),
                                          (0, 0), (100, 0), (0, 0), (0, 0)])
variable.setupGvar({".notdef": [delta], "box": [delta], "space": []})
variable.save(DESTINATION / "variable.ttf")
