#!/usr/bin/env python3
"""Fractional SVG sizes must affect painted text and preserve end anchoring."""
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
