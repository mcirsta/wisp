#!/usr/bin/env python3
"""Fractional CSS units reach both HTML layout and the Qt text painter."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/fractional-fonts.html"
names = ("integer15", "fraction155", "fraction159", "integer16", "em", "rem", "percent", "calc",
         "positive", "negative", "minimum", "wrap15", "wrap155")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-fractional-fonts-") as tmp:
    destination = Path(tmp)
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "420x440",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in names:
        args += ["--capture-id", name]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    elements = json.loads((destination / "inspection.json").read_text())["elements"]
    assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
    boxes = {e["id"]: e["boxes"][0] for e in elements}

    for name, size in (("integer15", 15), ("fraction155", 15.5), ("fraction159", 15.9), ("integer16", 16)):
        assert abs(boxes[name]["font"]["effective_pixel_size"] - size) < .002, (name, boxes[name]["font"])
    widths = [boxes[name]["content"]["width"] for name in names[:4]]
    assert widths == sorted(set(widths)), widths
    plain = boxes["fraction155"]["content"]["width"]
    characters = len("café alpha beta gamma")
    assert abs(boxes["positive"]["content"]["width"] - plain - characters * 2) <= 1
    assert abs(boxes["negative"]["content"]["width"] - plain + characters) <= 1
    minimum = meta["font_min_size_tenths_pt"] * 96 / 720
    assert abs(boxes["minimum"]["font"]["effective_pixel_size"] - minimum) < .002
    assert boxes["wrap15"]["content"]["height"] == 20, boxes["wrap15"]
    assert boxes["wrap155"]["content"]["height"] == 40, boxes["wrap155"]

    def pixels(name):
        box = boxes[name]["border_box"]
        geometry = f'{box["width"]}x{box["height"]}+{box["x"]}+{box["y"]}'
        return subprocess.run([magick, str(destination / "page.png"), "-crop", geometry,
                               "+repage", "-depth", "8", "RGB:-"],
                              check=True, capture_output=True, timeout=5).stdout

    reference = pixels("fraction155")
    assert min(reference) < 200, "Fractional text is blank"
    for name in ("em", "rem", "percent", "calc"):
        assert abs(boxes[name]["font"]["effective_pixel_size"] - 15.5) < .002, (name, boxes[name]["font"])
        assert boxes[name]["content"]["width"] == plain, name
        assert pixels(name) == reference, f"{name}: equivalent fractional size paints differently"

print("Fractional HTML fonts: px/em/rem/percent/calc, width, paint, wrapping, spacing and minimum size passed.")
