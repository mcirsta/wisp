#!/usr/bin/env python3
"""CSS position parsing and bytecode must survive painting and native restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 360, 820
RED, YELLOW = (255, 0, 0), (255, 255, 0)
# CSS Backgrounds and CSS Images: percentages multiply the difference between
# the 120x80 positioning area and the 20x10 image, independently on each axis.
CASES = [("corner", 100, 70), ("reordered", 100, 0), ("single", 50, 70),
         ("mixed", 50, 20), ("important", 2, 4), ("invalid", 2, 4),
         ("variable", 30, 28), ("shorthand", 0, 70)]


def verify(png, magick, moved):
    pixels = subprocess.run([magick, str(png), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == WIDTH * HEIGHT * 3
    for row, (name, dx, dy) in enumerate(CASES):
        if moved:
            dx, dy = 60, 14
        for kind, left in (("background", 20), ("object", 160)):
            # A small regular grid covers both image and background interiors;
            # omit their exact boundaries to avoid testing raster edge coverage.
            for y in range(1, 80, 2):
                for x in range(1, 120, 2):
                    if x in (dx, dx + 20) or y in (dy, dy + 10):
                        continue
                    expected = RED if dx < x < dx + 20 and dy < y < dy + 10 else YELLOW
                    offset = ((20 + row * 100 + y) * WIDTH + left + x) * 3
                    actual = tuple(pixels[offset:offset + 3])
                    assert actual == expected, (name, kind, moved, x, y, expected, actual)


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/position-values.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-position-values-") as tmp:
        for clicks in range(3):
            destination = Path(tmp) / str(clicks)
            args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{WIDTH}x{HEIGHT}",
                    "--capture-settle", "100", "--capture-timeout", "8000"]
            args += ["--capture-click-id", "switch"] * clicks
            process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
            assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
            meta = json.loads((destination / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], meta
            if clicks:
                steps = json.loads((destination / "steps.json").read_text())
                assert len(steps) == clicks + 1, steps
            verify(destination / "page.png", sys.argv[2], clicks == 1)
    print("Position values: 16 images passed initial, checked and restored pixel checks.")


if __name__ == "__main__":
    main()
