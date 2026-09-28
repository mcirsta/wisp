#!/usr/bin/env python3
"""Zero font sizes suppress glyphs, not descendants or explicit line heights."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/zero-font-size.html"
names = ("plain", "calc", "em", "inherited", "child", "control", "first", "second",
         "minimum", "spacing", "negative", "relative", "normal", "numeric", "explicit",
         "plain-row", "calc-row", "em-row", "inherited-row", "child-row", "control-row", "spacing-row")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-zero-font-size-") as tmp:
    destination = Path(tmp)
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "420x360",
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

    for name in ("plain", "calc", "em", "inherited", "spacing", "negative", "relative", "normal", "numeric", "explicit"):
        assert boxes[name]["font"]["effective_pixel_size"] == 0, (name, boxes[name]["font"])
    for name in ("plain", "calc", "em", "inherited"):
        assert boxes[name]["content"]["width"] == 0, (name, boxes[name]["content"])
        assert boxes[name]["content"]["height"] == 24, (name, boxes[name]["content"])
    assert boxes["spacing"]["content"]["width"] == 6, boxes["spacing"]["content"]
    assert boxes["negative"]["content"]["width"] == 0, boxes["negative"]["content"]
    for name in ("relative", "normal", "numeric"):
        assert boxes[name]["content"]["height"] == 0, (name, boxes[name]["content"])
    assert boxes["relative"]["content"]["width"] == 0, boxes["relative"]["content"]
    assert boxes["explicit"]["content"]["height"] == 20, boxes["explicit"]["content"]
    first, second = (boxes[name]["border_box"] for name in ("first", "second"))
    assert second["x"] == first["x"] + first["width"], (first, second)
    child, control = (boxes[name] for name in ("child", "control"))
    assert child["font"]["effective_pixel_size"] == 16, child["font"]
    assert child["border_box"]["x"] == control["border_box"]["x"], (child["border_box"], control["border_box"])
    assert child["content"]["width"] == control["content"]["width"]
    minimum = meta["font_min_size_tenths_pt"] * 96 / 720
    assert abs(boxes["minimum"]["font"]["effective_pixel_size"] - minimum) < .002

    def pixels(name):
        box = boxes[name]["border_box"]
        geometry = f'{box["width"]}x{box["height"]}+{box["x"]}+{box["y"]}'
        return subprocess.run([magick, str(destination / "page.png"), "-crop", geometry,
                               "+repage", "-depth", "8", "RGB:-"],
                              check=True, capture_output=True, timeout=5).stdout

    for name in ("plain-row", "calc-row", "em-row", "inherited-row", "spacing-row"):
        assert min(pixels(name)) == 255, f"{name}: zero-size text painted visible pixels"
    reference = pixels("control-row")
    assert min(reference) < 200, "Visible child control is blank"
    assert pixels("child-row") == reference, "Zero-size parent text changed the child's placement or painting"

print("Zero font sizes: units, inheritance, widths, whitespace, line heights, spacing, painting and minimum passed.")
