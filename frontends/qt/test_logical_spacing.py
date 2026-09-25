#!/usr/bin/env python3
"""Check logical block spacing using the real renderer and local assets only."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/logical-spacing.html"
# Border-box size, padding TRBL, and vertical margins. Expected geometry is
# independent of font metrics and checked against Chromium by compare_capture.py.
expected = {
    "single": ((160, 80), (20, 0, 20, 0), (0, 0)),
    "pair": ((160, 80), (10, 0, 30, 0), (0, 0)),
    "variable": ((160, 80), (20, 0, 20, 0), (0, 0)),
    "overrides": ((160, 62), (7, 0, 15, 0), (0, 0)),
    "important": ((160, 80), (20, 0, 20, 0), (0, 0)),
    "percentage": ((160, 100), (20, 0, 40, 0), (0, 0)),
    "margins": ((160, 40), (0, 0, 0, 0), (12, 24)),
    "edges": ((160, 52), (5, 0, 7, 0), (8, 16)),
    "inherited": ((160, 64), (11, 0, 13, 0), (0, 0)),
    "inline": ((200, 40), (0, 30, 0, 10), (0, 0)),
    "flex": ((160, 70), (10, 0, 20, 0), (0, 0)),
    "grid": ((160, 70), (10, 0, 20, 0), (0, 0)),
    "invalid": ((160, 60), (10, 0, 10, 0), (0, 0)),
    "ratio": ((200, 100), (10, 5, 20, 5), (0, 0)),
    "header": ((400, 96), (20, 16, 20, 16), (0, 0)),
}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-logical-spacing-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "1280x900",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in expected:
        args.extend(("--capture-id", element_id))
    args.extend(("--capture-id", "after-margins"))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True,
                             text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    boxes = {item["id"]: item["boxes"][0] for item in report["elements"]}
    failures = []
    for element_id, (size, padding, margins) in expected.items():
        box = boxes[element_id]
        rect = box["border_box"]
        actual = ((rect["width"], rect["height"]), tuple(box["padding_trbl"]),
                  (box["margin_trbl"][0], box["margin_trbl"][2]))
        if actual != (size, padding, margins):
            failures.append(f"{element_id}: expected {(size, padding, margins)}, got {actual}")
    margin_box = boxes["margins"]["border_box"]
    gap = boxes["after-margins"]["border_box"]["y"] - margin_box["y"] - margin_box["height"]
    assert gap == 24, f"Block margin gap: expected 24, got {gap}"
    assert not failures, "\n".join(failures)
print(f"Logical spacing: {len(expected)} boxes and the following sibling's position passed.")
