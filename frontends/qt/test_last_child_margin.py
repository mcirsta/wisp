#!/usr/bin/env python3
"""A last child's margin contributes to auto height before bottom edges."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/last-child-bottom-margin.html"
# Border-box height = 2px top border + 3px top padding + content height
# + bottom padding/border. All geometry is authored in pixels, not font metrics.
heights = {"padding": 43, "border": 39, "both": 44, "negative": 23, "clamped": 10,
           "nested": 50, "mixed": 37, "nested-negative": 17,
           "fixed-short": 21, "fixed-tall": 71, "min-short": 44, "min-tall": 71,
           "ratio": 44, "bfc": 44, "float": 44}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-last-child-margin-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "600x1500",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in heights:
        for suffix in ("", "-after"):
            args.extend(("--capture-id", name + suffix))
    process = subprocess.run([*args, str(fixture)], env=env,
                             capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    for element in report["elements"]:
        assert element["status"] == "ok" and element.get("boxes"), (
            f"Missing rendered box for {element['id']}: {element}")
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
    for name, height in heights.items():
        box, after = boxes[name], boxes[name + "-after"]
        assert box["height"] == height, (name, box, height)
        gap = after["y"] - box["y"] - height
        # Ordinary sibling margins collapse max(7, 11); the float's clear
        # sibling starts at the float's bottom margin edge (7px).
        expected_gap = 7 if name == "float" else 11
        assert gap == expected_gap, (name, gap, expected_gap)

print("Last child margins: bottom edges, negative margins, nesting and height constraints passed.")
