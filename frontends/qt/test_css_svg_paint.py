#!/usr/bin/env python3
"""SVG 2 paint properties through HTML CSS selection and the inline-SVG bridge."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 640, 230
INK, WHITE, BLACK, RED = (0, 34, 68), (255, 255, 255), (0, 0, 0), (255, 0, 0)
HALF, OVERLAP = (127, 144, 161), (63, 89, 114)
# Interior samples derive from SVG 2 paint values and an 8px centered stroke.
SAMPLES = [
    ("initial black", 40, 40, BLACK), ("none", 110, 40, WHITE),
    ("currentColor", 180, 40, INK), ("important fill and following variable", 250, 40, RED),
    ("user-unit stroke width", 297, 40, INK), ("user-unit stroke interior", 310, 40, WHITE),
    ("invalid angle fallback", 367, 40, INK), ("tiny negative fallback", 437, 40, INK),
    ("zero fill opacity", 530, 40, WHITE), ("zero stroke opacity", 577, 40, WHITE),
    ("explicit initial fill", 40, 110, BLACK), ("fill opacity above one", 110, 110, INK),
    ("fill opacity below zero", 180, 110, WHITE), ("stroke opacity above one", 227, 110, INK),
    ("stroke opacity below zero", 297, 110, WHITE),
    ("important stroke and following variable", 367, 110, INK),
    ("half fill opacity", 460, 110, HALF), ("half stroke opacity", 507, 110, HALF),
    ("separate translucent shapes", 25, 180, HALF), ("translucent overlap", 35, 180, OVERLAP),
    ("zero dash opacity", 95, 180, WHITE), ("half dash opacity", 165, 180, HALF),
]

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/css-svg-paint.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-css-svg-paint-") as tmp:
    for quirks in (False, True):
        page = Path(tmp) / f"svg-paint-{quirks}.html"
        source = fixture.read_text()
        page.write_text(source.replace("<!doctype html>", "") if quirks else source)
        destination = Path(tmp) / f"capture-{quirks}"
        process = subprocess.run([str(binary), "--capture-dir", str(destination),
                                  "--capture-size", f"{WIDTH}x{HEIGHT}", "--capture-settle", "100",
                                  "--capture-timeout", "8000", str(page)],
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
            # Permit one channel step for alpha-compositing roundoff only.
            tolerance = 1 if expected in (HALF, OVERLAP) else 0
            if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
                failures.append(f"{name} at {x},{y}: expected {expected}, got {actual}")
        assert not failures, f"quirks={quirks}:\n" + "\n".join(failures)
print(f"CSS SVG paint: {len(SAMPLES)} pixel samples passed in both document modes.")
