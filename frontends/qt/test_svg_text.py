#!/usr/bin/env python3
"""Check fractional SVG text sizes, anchoring and glyph rotation in the real painter."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/svg-text-sizing.html"
width, height = 600, 180
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

def capture_pixels(fixture, width, height):
    with tempfile.TemporaryDirectory(prefix="wisp-svg-text-") as tmp:
        destination = Path(tmp) / "capture"
        process = subprocess.run([str(binary), "--capture-dir", str(destination),
            "--capture-size", f"{width}x{height}", "--capture-settle", "100",
            "--capture-timeout", "8000", str(fixture)],
            env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                check=True, capture_output=True, timeout=5).stdout
        assert len(pixels) == width * height * 3

        return pixels


pixels = capture_pixels(fixture, width, height)

def ink_bounds(left, top, right, bottom):
    xs = [x for y in range(top, bottom) for x in range(left, right)
          if min(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) < 200]
    assert xs, f"Missing SVG text in {left, top, right, bottom}"
    return min(xs), max(xs)

for scale, top in ((1, 0), (0.6, 100)):
    widths = []
    for baseline in (20, 50, 80):
        bottom = top + round(baseline * scale) + 1
        row_top = bottom - round(15 * scale) - 1
        left = ink_bounds(0, row_top, round(250 * scale), bottom)
        right = ink_bounds(round(250 * scale), row_top, round(550 * scale), bottom)
        text_width = left[1] - left[0] + 1
        widths.append(text_width)
        assert abs((right[1] - right[0] + 1) - text_width) <= 1, (scale, left, right)
        # Larger text should move its left edge, retaining the same end anchor.
        assert abs(right[1] - round(520 * scale)) <= 2, (scale, right)
    assert widths[0] < widths[1] < widths[2], (scale, widths)

print("SVG text: fractional sizes and end anchors passed at 100% and 60%.")

fixture = fixture.with_name("svg-text-rotation.html")
width, height = 600, 360
pixels = capture_pixels(fixture, width, height)
colors = [(255, 0, 0), (0, 170, 0), (0, 0, 255), (255, 0, 255), (0, 170, 170), (0, 0, 0)]

for scale, top in ((1, 0), (0.6, 220)):
    bounds = []
    for color in colors:
        points = []
        for y in range(top, top + round(220 * scale)):
            for x in range(round(600 * scale)):
                i = (y * width + x) * 3
                if all(abs(pixels[i + channel] - color[channel]) <= 20 for channel in range(3)):
                    points.append((x, y - top))
        assert points, f"Missing text/marker with color {color} at scale {scale}"
        bounds.append((min(x for x, y in points), min(y for x, y in points),
                       max(x for x, y in points), max(y for x, y in points)))
    red, green, blue, magenta, cyan, marker = bounds
    text_width = red[2] - red[0]
    # The same glyphs change orientation, retaining their size and anchor.
    for rotated in (green, blue):
        assert rotated[3] - rotated[1] > 2 * (rotated[2] - rotated[0]), (scale, rotated)
        assert abs(rotated[3] - rotated[1] - text_width) <= 3, (scale, rotated, red)
    assert abs((green[1] + green[3]) / 2 - 70 * scale) <= 3, (scale, green)
    assert abs(blue[1] - 100 * scale) <= 3, (scale, blue)
    assert abs(magenta[0] - 450 * scale) <= 3, (scale, magenta)
    assert abs(magenta[2] - magenta[0] - text_width) <= 3, (scale, magenta, red)
    # Inherited tspan position plus dx/dy are transformed in local coordinates.
    assert abs(cyan[1] - 141 * scale) <= 3, (scale, cyan)
    assert 137 * scale - 1 <= cyan[0] < cyan[2] <= 160 * scale + 1, (scale, cyan)
    assert cyan[3] - cyan[1] > 2 * (cyan[2] - cyan[0]), (scale, cyan)
    assert abs(marker[0] - 570 * scale) <= 1 and abs(marker[1] - 10 * scale) <= 1, (scale, marker)

print("SVG text: rotations, anchors, nested offsets and painter restoration passed at 100% and 60%.")
