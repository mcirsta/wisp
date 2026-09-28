#!/usr/bin/env python3
"""Visible descendants paint even when their positioned host is empty or offscreen."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/visible-overflow.html"
cases = ("left", "right", "pseudo", "block", "zero-width", "nested", "absolute",
         "fixed", "offscreen", "hidden-zero", "hidden", "clip")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-visible-overflow-") as tmp:
    for zoom in (1, 1.5):
        width, height = int(560 * zoom), int(800 * zoom)
        destination = Path(tmp) / str(zoom)
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
                "--capture-settle", "100", "--capture-timeout", "8000", f"--scale={int(100 * zoom)}"]
        for name in cases:
            args += ["--capture-id", name, "--capture-id", name + "-host"]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, (zoom, process.stderr)
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], (zoom, meta)
        assert meta["zoom"] == zoom, meta
        elements = json.loads((destination / "inspection.json").read_text())["elements"]
        assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
        boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}
        pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                check=True, capture_output=True, timeout=5).stdout
        assert len(pixels) == width * height * 3

        for name in cases:
            case, host = boxes[name], boxes[name + "-host"]
            if name in ("left", "right", "pseudo", "block", "nested", "absolute", "fixed", "hidden-zero"):
                assert host["height"] == 0, (zoom, name, host)
            if name in ("zero-width", "absolute", "fixed"):
                assert host["width"] == 0, (zoom, name, host)
            # Expectations are in CSS pixels, independently of the host's size.
            left = case["x"] + (310 if name == "right" else 10)
            top = case["y"] + 8
            square_width, square_height = (10, 12) if name == "hidden" else (24, 24)
            if name in ("hidden-zero", "clip"):
                square_width = square_height = 0
            bounds = tuple(int(v * zoom) for v in (left, top, left + square_width, top + square_height))
            # Check the complete row: this catches both missing pixels and
            # unwanted painting outside overflow:hidden or clip:rect bounds.
            for y in range(int(case["y"] * zoom), int((case["y"] + 60) * zoom)):
                for x in range(int(case["x"] * zoom), int((case["x"] + 480) * zoom)):
                    inside = bounds[0] <= x < bounds[2] and bounds[1] <= y < bounds[3]
                    expected = (22, 136, 68) if inside else (255, 255, 255)
                    offset = (y * width + x) * 3
                    actual = tuple(pixels[offset:offset + 3])
                    assert actual == expected, (zoom, name, x, y, actual, expected)

print("Visible overflow: empty left/right floats, generated content, positioned hosts, clipping and zoom passed.")
