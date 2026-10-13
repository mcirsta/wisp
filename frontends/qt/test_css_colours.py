#!/usr/bin/env python3
"""Colour values through the native CSS parser and real Qt painting."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 520, 730
# CSS Color 4 clamping and nearest-integer rounding for the ARGB8 painter.
# Sample flat interiors so text, antialiasing and edge coverage cannot affect them.
SAMPLES = [
    ("hex", 50, 30, (192, 255, 238)),
    ("fractional channels", 150, 30, (101, 21, 1)),
    ("mixed components", 250, 30, (128, 40, 153)),
    ("half alpha on white", 350, 30, (255, 127, 127)),
    ("large clamped value", 450, 30, (255, 0, 0)),
    ("missing alpha", 50, 90, (255, 255, 255)),
    ("real color(srgb) replaces fallback", 150, 90, (255, 0, 0)),
    ("unknown function fallback", 250, 90, (0, 34, 68)),
    ("invalid RGB fallback", 350, 90, (0, 34, 68)),
    ("gradient colour", 450, 90, (128, 0, 0)),
    ("top border", 50, 135, (255, 0, 0)),
    ("right border", 85, 150, (0, 255, 0)),
    ("bottom border", 50, 165, (0, 0, 255)),
    ("left border", 15, 150, (128, 0, 128)),
    ("HSL conversion", 150, 150, (89, 166, 89)),
    ("HSL alpha", 250, 150, (255, 127, 127)),
    ("HWB conversion", 350, 150, (51, 230, 140)),
    ("HWB normalization", 450, 150, (170, 170, 170)),
    ("large hue", 50, 210, (128, 255, 0)),
    ("HWB gradient", 150, 210, (170, 170, 170)),
    ("invalid HWB fallback", 250, 210, (0, 34, 68)),
    ("HSL missing alpha", 350, 210, (255, 255, 255)),
    ("HSL border", 450, 195, (89, 166, 89)),
    ("HWB border", 485, 210, (51, 230, 140)),
    ("unsupported gradient fallback", 50, 270, (0, 34, 68)),
    ("unsupported second layer fallback", 150, 270, (0, 34, 68)),
    ("border shorthand fallback", 215, 270, (0, 34, 68)),
    ("discarded shorthand width", 228, 270, (170, 187, 204)),
    ("SVG CSS fill fallback", 350, 270, (0, 34, 68)),
    ("SVG CSS stroke fallback", 315, 270, (192, 255, 238)),
    ("transparent black still works", 450, 270, (255, 255, 255)),
    ("unsupported substituted colour inherits", 50, 330, (0, 34, 68)),
    ("embedded CSS-wide shorthand fallback", 115, 330, (0, 34, 68)),
    ("tiny negative width fallback", 215, 330, (0, 34, 68)),
    ("grid unit width fallback", 315, 330, (0, 34, 68)),
    ("omitted border colour", 415, 330, (0, 34, 68)),
    ("missing HSL hue paints as zero", 50, 390, (255, 0, 0)),
    ("missing HSL saturation paints as zero", 150, 390, (128, 128, 128)),
    ("missing HSL lightness paints as zero", 250, 390, (0, 0, 0)),
    ("missing HWB whiteness paints as zero", 350, 390, (0, 204, 0)),
    ("missing HWB blackness paints as zero", 450, 390, (51, 51, 255)),
    ("lab-gray", 50, 450, (119, 119, 119)),
    ("lch-gray", 150, 450, (119, 119, 119)),
    ("lab-alpha", 250, 450, (187, 187, 187)),
    ("oklab-gray", 350, 450, (99, 99, 99)),
    ("oklab-alpha", 450, 450, (177, 177, 177)),
    ("lab-red", 50, 510, (255, 0, 0)),
    ("lab-blue", 150, 510, (0, 0, 255)),
    ("oklab-red", 250, 510, (255, 0, 0)),
    ("oklch-red", 350, 510, (255, 0, 0)),
    ("lch-negative-chroma", 450, 510, (119, 119, 119)),
    ("lab-high-lightness", 50, 570, (255, 255, 255)),
    ("lab-low-lightness", 150, 570, (0, 0, 0)),
    ("lab-missing-alpha", 250, 570, (255, 255, 255)),
    ("lch-length-fallback", 350, 570, (0, 34, 68)),
    ("oklch-percent-fallback", 450, 570, (0, 34, 68)),
    ("sRGB gray", 50, 630, (128, 128, 128)),
    ("sRGB alpha", 150, 630, (255, 127, 127)),
    ("sRGB missing components", 250, 630, (0, 128, 0)),
    ("sRGB missing alpha", 350, 630, (255, 255, 255)),
    ("sRGB percentages", 450, 630, (128, 64, 0)),
    ("sRGB gradient", 50, 690, (0, 0, 255)),
    ("sRGB border", 115, 690, (255, 0, 0)),
    ("sRGB variable substitution", 250, 690, (51, 102, 153)),
    ("sRGB SVG fill", 350, 690, (255, 0, 0)),
    ("sRGB SVG stroke", 315, 690, (0, 0, 255)),
    ("sRGB transparent black", 450, 690, (255, 255, 255)),
]

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/css-colours.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-css-colours-") as tmp:
    destination = Path(tmp) / "capture"
    process = subprocess.run([str(binary), "--capture-dir", str(destination),
                              "--capture-size", f"{WIDTH}x{HEIGHT}", "--capture-settle", "100",
                              "--capture-timeout", "8000", str(fixture)],
                             env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    pixels = subprocess.run([sys.argv[2], str(destination / "page.png"), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == WIDTH * HEIGHT * 3
    failures = []
    for name, x, y, expected in SAMPLES:
        offset = (y * WIDTH + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        if actual != expected:
            failures.append(f"{name} at {x},{y}: expected {expected}, got {actual}")
    assert not failures, "\n".join(failures)
print(f"CSS colours: {len(SAMPLES)} pixel samples passed.")
