#!/usr/bin/env python3
"""Layer order, resource loading, alpha, list matching and clipping in real pixels."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 760, 830
RED, BLUE, YELLOW, PURPLE = (255, 0, 0), (0, 0, 255), (255, 255, 0), (128, 0, 127)
# Coordinates are deliberately away from edges and text. Expected colors follow
# CSS compositing/order, independent of either browser implementation.
SAMPLES = [
    ("order", 60, 80, PURPLE), ("order far", 160, 80, PURPLE),
    ("image base", 255, 35, YELLOW), ("first image", 265, 45, RED),
    ("overlap", 275, 55, RED), ("second image", 285, 65, BLUE), ("image clip", 295, 75, YELLOW),
    ("none base", 590, 80, YELLOW), ("none right", 680, 120, RED), ("none left", 500, 120, BLUE),
    ("cycle alpha PNG", 20, 170, PURPLE), ("cycle second", 200, 250, BLUE), ("cycle base", 100, 210, YELLOW),
    ("repeat overlap", 260, 170, RED), ("repeat x", 400, 170, RED),
    ("repeat y", 260, 210, BLUE), ("repeat base", 400, 210, YELLOW),
    ("mixed image", 500, 170, RED), ("mixed radial", 530, 210, BLUE), ("mixed alpha", 650, 210, (0, 0, 127)),
    ("invalid preserves", 110, 340, BLUE), ("shorthand first", 440, 380, RED),
    ("shorthand second", 260, 300, BLUE), ("shorthand base", 350, 340, YELLOW),
    ("variable important", 590, 340, BLUE), ("inline", 20, 420, PURPLE),
    ("pseudo", 350, 470, PURPLE), ("shift base", 500, 430, YELLOW),
    ("shift lower", 500, 470, RED), ("shift upper", 550, 470, BLUE),
    ("missing image", 110, 600, BLUE), ("reset", 350, 600, (0, 128, 0)),
    ("table first cell", 530, 600, (64, 128, 64)), ("table second cell", 650, 600, PURPLE),
    ("table spacing", 590, 600, (238, 238, 238)), ("root canvas", 740, 680, (238, 238, 238)),
    ("pair x left", 20, 740, RED), ("pair x right", 200, 740, RED),
    ("pair x above", 50, 710, YELLOW), ("pair x below", 50, 770, YELLOW),
    ("pair y top", 300, 700, RED), ("pair y bottom", 300, 780, RED),
    ("pair y left", 270, 730, YELLOW), ("pair y right", 430, 730, YELLOW),
    ("pair both", 500, 700, RED), ("pair both far", 680, 780, RED),
]


def verify(png, magick):
    pixels = subprocess.run([magick, str(png), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == WIDTH * HEIGHT * 3
    failures = []
    for name, x, y, expected in SAMPLES:
        offset = (y * WIDTH + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        if any(abs(a - b) > 2 for a, b in zip(actual, expected)):
            failures.append(f"{name} at {x},{y}: expected {expected}, got {actual}")
    assert not failures, "\n".join(failures)


def main():
    binary = Path(sys.argv[1]).resolve()
    magick = sys.argv[2]
    fixture = Path(__file__).resolve().parents[2] / "test/background-layers.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-background-layers-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{WIDTH}x{HEIGHT}",
                "--capture-settle", "100", "--capture-timeout", "8000", str(fixture)]
        process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        verify(destination / "page.png", magick)
    print(f"Background layers: {len(SAMPLES)} pixel samples passed.")


if __name__ == "__main__":
    main()
