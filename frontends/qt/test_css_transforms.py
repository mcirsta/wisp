#!/usr/bin/env python3
"""CSS Transforms 1/2 grammar and painting, using specification-derived geometry."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 640, 180
INK, WHITE = (0, 34, 68), (255, 255, 255)
# Flat interior/exterior samples avoid antialiasing at transformed edges.
SAMPLES = [
    ("translate", 35, 40, INK), ("translate old position", 15, 40, WHITE),
    ("translateY", 100, 60, INK), ("translateY old position", 100, 35, WHITE),
    ("scale", 165, 25, INK), ("scale exterior", 155, 25, WHITE),
    ("scaleX", 245, 40, INK), ("scaleX height", 245, 25, WHITE),
    ("scaleY", 340, 25, INK), ("scaleY width", 325, 25, WHITE),
    ("rotate", 430, 25, INK), ("rotate exterior", 415, 40, WHITE),
    ("percentage translate", 515, 55, INK), ("percentage old position", 500, 40, WHITE),
    ("zero second scale hides content", 580, 40, WHITE),
]
for name, left in (("angle", 10), ("grid", 90), ("second grid", 170), ("unitless", 250)):
    SAMPLES.extend([(name + " fallback", left + 25, 130, INK),
                    (name + " old position", left + 5, 130, WHITE)])

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/css-transforms.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-css-transforms-") as tmp:
    for quirks in (False, True):
        page = Path(tmp) / f"transforms-{quirks}.html"
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
            if actual != expected:
                failures.append(f"{name} at {x},{y}: expected {expected}, got {actual}")
        assert not failures, f"quirks={quirks}:\n" + "\n".join(failures)
print(f"CSS transforms: {len(SAMPLES)} pixel samples passed in both document modes.")
