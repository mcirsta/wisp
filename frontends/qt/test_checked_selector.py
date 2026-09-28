#!/usr/bin/env python3
"""Checked controls reveal content during initial layout, with scripts off."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/checked-selector.html"
heights = {
    "tabs": 90, "selected-label": 20, "selected-panel": 40,
    "other-label": 30, "checkbox-label": 20, "checkbox-panel": 40,
    "unchecked-label": 30, "disabled-label": 20, "disabled-panel": 40,
    "text-label": 30, "default-label": 30, "sibling-on": 20,
    "after-tabs": 10,
}
hidden = ("other-panel", "unchecked-panel", "text-panel", "default-panel", "sibling-off")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-checked-selector-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "500x650",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in (*heights, *hidden):
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True,
                             text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    elements = {item["id"]: item for item in report["elements"]}
    for element_id, height in heights.items():
        boxes = elements[element_id]["boxes"]
        assert boxes, f"Missing layout box for {element_id}"
        actual = boxes[0]["content"]["height"]
        assert actual == height, f"{element_id}: expected height {height}, got {actual}"
    for element_id in hidden:
        assert not elements[element_id]["boxes"], f"Unchecked panel {element_id} should be hidden"
    after = elements["after-tabs"]["boxes"][0]["border_box"]
    assert after["y"] == 100, f"Content below tabs should follow the visible panel: {after}"
print("Checked selectors: selected content visible, unchecked content hidden, following content positioned correctly.")
