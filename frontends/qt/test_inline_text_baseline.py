#!/usr/bin/env python3
"""Mixed text line geometry, wrapping and painted baseline with a bundled font."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/inline-text-baseline.html"
names = ("mixed", "large-text", "small-text", "strut", "zero-strut", "same-font",
         "wrapped", "empty", "out-of-flow")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
width, height = 300, 430
with tempfile.TemporaryDirectory(prefix="wisp-text-baseline-") as tmp:
    destination = Path(tmp)
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in names:
        args += ["--capture-id", name]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    elements = json.loads((destination / "inspection.json").read_text())["elements"]
    assert {e["id"] for e in elements} == set(names), elements
    assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}

    # Exact heights use the bundled font's metrics and explicit pixel line-heights.
    for name, expected in (("mixed", 44), ("strut", 44), ("zero-strut", 40),
                           ("same-font", 40), ("wrapped", 132), ("empty", 0), ("out-of-flow", 0)):
        assert boxes[name]["height"] == expected, (name, boxes[name], expected)
    assert boxes["small-text"]["y"] - boxes["large-text"]["y"] == 8, boxes

    pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == width * height * 3

    # Inspection reports each span's zero-width opening marker. The bundled
    # glyph advance is 600/1000 em, so scan 24px and 12px of text respectively.
    def ink_bottom(name, advance):
        box = boxes[name]
        ys = [y for y in range(box["y"], box["y"] + box["height"])
              for x in range(box["x"], box["x"] + advance)
              if max(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) < 100]
        assert ys, f"{name}: missing glyph ink"
        return max(ys) + 1

    assert ink_bottom("large-text", 24) == ink_bottom("small-text", 12), boxes

print("Mixed text baselines: strut, wrapping, controls, empty fragments and painting passed.")
