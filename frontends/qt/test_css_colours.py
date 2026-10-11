#!/usr/bin/env python3
"""Colour values through the native CSS parser and real Qt painting."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 520, 190
# CSS Color 4 clamping and nearest-integer rounding for the ARGB8 painter.
# Sample flat interiors so text, antialiasing and edge coverage cannot affect them.
SAMPLES = [
    ("hex", 50, 30, (192, 255, 238)),
    ("fractional channels", 150, 30, (101, 21, 1)),
    ("mixed components", 250, 30, (128, 40, 153)),
    ("half alpha on white", 350, 30, (255, 127, 127)),
    ("large clamped value", 450, 30, (255, 0, 0)),
    ("missing alpha", 50, 90, (255, 255, 255)),
    ("unsupported color() fallback", 150, 90, (0, 34, 68)),
    ("unknown function fallback", 250, 90, (0, 34, 68)),
    ("invalid RGB fallback", 350, 90, (0, 34, 68)),
    ("gradient colour", 450, 90, (128, 0, 0)),
    ("top border", 50, 135, (255, 0, 0)),
    ("right border", 85, 150, (0, 255, 0)),
    ("bottom border", 50, 165, (0, 0, 255)),
    ("left border", 15, 150, (128, 0, 128)),
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
