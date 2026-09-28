#!/usr/bin/env python3
"""Positioned ::before content must normalize, paint and stay out of text flow."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/positioned-pseudo.html"
cases = ("absolute", "inline-block", "block", "float", "text", "relative",
         "block-owner", "flow", "flow-inline-block", "fixed")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-positioned-pseudo-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "500x760",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in cases:
        args += ["--capture-id", name, "--capture-id", name + "-reference"]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    elements = json.loads((destination / "inspection.json").read_text())["elements"]
    assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}

    def pixels(box):
        geometry = f'{box["width"]}x{box["height"]}+{box["x"]}+{box["y"]}'
        return subprocess.run([magick, str(destination / "page.png"), "-crop", geometry,
                               "+repage", "-depth", "8", "RGB:-"],
                              check=True, capture_output=True, timeout=5).stdout

    for name in cases:
        actual, reference = boxes[name], boxes[name + "-reference"]
        assert (actual["width"], actual["height"]) == (220, 60), (name, actual)
        assert (reference["width"], reference["height"]) == (220, 60), (name, reference)
        actual_pixels, reference_pixels = pixels(actual), pixels(reference)
        assert actual_pixels == reference_pixels, f"{name}: pseudo-element differs from real element"
        if name != "fixed":
            assert actual_pixels.count(bytes((22, 136, 68))) > 5, f"{name}: generated content was not painted"

    # Fixed content uses the viewport, independently of its owner's row.
    actual = {"x": 20, "y": 700, "width": 24, "height": 40}
    reference = dict(actual, x=240)
    actual_pixels = pixels(actual)
    assert actual_pixels == pixels(reference), "fixed: incorrect painting or viewport positioning"
    assert actual_pixels.count(bytes((22, 136, 68))) > 800, "fixed: missing generated content"

print("Positioned pseudo-elements: absolute/fixed, display variants, floats, inline ancestors and flow controls passed.")
