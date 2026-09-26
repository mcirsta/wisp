#!/usr/bin/env python3
"""Inline image geometry and painted text baseline through the Qt frontend."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/inline-image-baseline.html"
ids = ("baseline", "baseline-image", "top", "bottom", "mixed", "mixed-image", "two",
       "tall-image", "short-image", "small", "small-image", "large-font", "zero-font",
       "zero-line", "edges", "edges-image", "linked", "wrapped", "wrap-first", "wrap-second",
       "block", "float-only")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
width, height = 650, 1500
with tempfile.TemporaryDirectory(prefix="wisp-inline-baseline-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in ids:
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    report = json.loads((destination / "inspection.json").read_text())
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}

    def bottom(name):
        return boxes[name]["y"] + boxes[name]["height"]

    gap = boxes["baseline"]["height"] - 80
    assert 0 < gap < 23, gap  # The exact font descent depends on installed fonts.
    for name in ("top", "bottom", "zero-font", "zero-line", "block"):
        assert boxes[name]["height"] == 80, (name, boxes[name])
    for name in ("mixed", "two", "linked"):
        assert boxes[name]["height"] == 80 + gap, (name, boxes[name], gap)
    assert bottom("tall-image") == bottom("short-image")
    assert boxes["small"]["height"] == 23
    assert bottom("small") - bottom("small-image") == gap
    assert boxes["large-font"]["height"] > boxes["baseline"]["height"]
    assert boxes["edges"]["height"] == 102 + gap
    assert bottom("edges") - bottom("edges-image") == 7 + gap
    assert boxes["wrapped"]["height"] == 2 * (80 + gap)
    assert boxes["wrap-second"]["y"] - boxes["wrap-first"]["y"] == 80 + gap
    assert boxes["float-only"]["height"] == 0

    # The capital H has its ink bottom on the baseline. Check actual painting,
    # not just box placement: it must line up with the adjacent image bottom.
    pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == width * height * 3
    text_left = boxes["mixed-image"]["x"] + boxes["mixed-image"]["width"]
    ink_y = [y for y in range(boxes["mixed"]["y"], bottom("mixed"))
             for x in range(text_left, text_left + 50)
             if max(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) < 100]
    assert ink_y and abs(max(ink_y) + 1 - bottom("mixed-image")) <= 1, (ink_y, boxes["mixed-image"])

print("Inline baseline: image spacing, alignment, wrapping, and painted text passed.")
