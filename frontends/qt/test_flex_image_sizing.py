#!/usr/bin/env python3
"""Check flex image sizing through CSS parsing, intrinsic measurement and layout."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/flex-image-sizing.html"
# Content-box dimensions, independently checked against Chromium. Avoid text
# metrics and page coordinates so this also works with different installed fonts.
expected = {
    "block-image": (120, 60),
    "stretch-item": (120, 60),
    "stretch-image": (120, 60),
    "tall-item": (300, 150),
    "tall-image": (300, 150),
    "explicit-item": (120, 60),
    "explicit-image": (120, 60),
    "start-item": (200, 100),
    "start-image": (200, 100),
    "minimum-item": (160, 80),
    "minimum-image": (160, 80),
    "maximum-item": (160, 80),
    "maximum-image": (160, 80),
    "edges-item": (160, 80),
    "edges-image": (160, 80),
    "auto-margin-item": (200, 100),
    "auto-margin-image": (200, 100),
    "nested-item": (120, 60),
    "nested-image": (120, 60),
    "auto-height-item": (200, 150),
    "auto-height-image": (200, 150),
    "container-minimum-item": (200, 150),
    "container-minimum-image": (200, 150),
    "fit-minimum-item": (120, 60),
    "fit-minimum-image": (120, 60),
    "zero-minimum-item": (50, 60),
    "zero-minimum-image": (50, 60),
    "nested-inner": (120, 60),
    "wrapped-item": (200, 100), "wrapped-image": (200, 100),
    "wrapped-two-item": (200, 150), "wrapped-two-image": (200, 150),
    "min-content": (120, 20), "max-content": (200, 10),
    "fit-content": (150, 20), "border-keyword": (200, 10),
    "column-keyword": (200, 10),
    "basis-percent-padding": (120, 60),
    "parent-minimum-item": (160, 80), "parent-minimum-image": (160, 80),
    "parent-maximum-item": (160, 80), "parent-maximum-image": (160, 80),
    "inline-item": (120, 60), "inline-image": (120, 60),
    "reverse-item": (120, 60), "reverse-image": (120, 60),
    "percent-maximum-item": (60, 60), "percent-maximum-image": (60, 30),
    "maximum-keyword": (120, 20),
    "stretched-ratio": (120, 60),
}

env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-flex-images-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x3300",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in expected:
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True,
                             text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    for item in report["elements"]:
        assert item["boxes"], f"Missing layout box for {item['id']}"
    actual = {item["id"]: item["boxes"][0]["content"] for item in report["elements"]}
    failures = []
    for element_id, size in expected.items():
        rect = actual[element_id]
        measured = rect["width"], rect["height"]
        if measured != size:
            failures.append(f"{element_id}: expected {size}, got {measured}")
    assert not failures, "\n".join(failures)
print(f"Flex image sizing: {len(expected)} geometry checks passed.")
