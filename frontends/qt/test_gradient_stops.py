#!/usr/bin/env python3
"""Check actual gradient pixels using local assets; ImageMagick decodes the PNG."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/gradient-length-stops.html"
width, height = 920, 600

# Samples at x=25,75,150, y=50 within each 200x100 box. Colors follow the
# specified stop positions, independently of the renderer's implementation.
# A small tolerance accommodates raster rounding and the fallback's strips.
samples = {
    "pixels": [(65, 65, 65), (193, 193, 193), (255, 255, 255)],
    "percent": [(33, 33, 33), (97, 97, 97), (192, 192, 192)],
    "mixed": [(237, 0, 18), (78, 0, 177), (0, 0, 255)],
    "omitted": [(47, 47, 47), (38, 38, 38), (0, 0, 0)],
    "hard": [(255, 0, 0), (0, 0, 255), (0, 0, 255)],
    "outside": [(80, 80, 80), (112, 112, 112), (160, 160, 160)],
    "alpha": [(190, 190, 255), (62, 62, 255), (0, 0, 255)],
    "important": [(65, 65, 65), (193, 193, 193), (255, 255, 255)],
    "relative": [(65, 65, 65), (193, 193, 193), (255, 255, 255)],
    "reverse": [(255, 255, 255), (255, 255, 255), (126, 126, 126)],
    "vertical": [(184, 184, 184)] * 3,
    "radial": [(255, 255, 255), (125, 125, 125), (255, 255, 255)],
}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-gradient-stops-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
            "--capture-settle", "100", "--capture-timeout", "8000", str(fixture)]
    process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == width * height * 3
    failures = []

    def check(name, x, y, expected):
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        if any(abs(a - b) > 8 for a, b in zip(actual, expected)):
            failures.append(f"{name} at {x},{y}: expected {expected}, got {actual}")

    for i, (name, colors) in enumerate(samples.items()):
        left, top = 10 + i % 4 * 220, 30 + i // 4 * 130
        for x, expected in zip((25, 75, 150), colors):
            check(name, left + x, top + 50, expected)
    check("clipped", 110, 470, (64, 64, 64))
    check("shifted", 330, 470, (128, 128, 128))
    # The neighboring area must remain outside the gradient's clip.
    check("outside clip", 220, 470, (255, 255, 255))
    check("alpha rectangle", 550, 470, (127, 127, 255))
    assert not failures, "\n".join(failures)
print("Gradient stops: 40 pixel samples passed.")
