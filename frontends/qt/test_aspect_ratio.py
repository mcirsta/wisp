#!/usr/bin/env python3
"""Exercise preferred-ratio layout through the real engine; no Chromium needed."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/aspect-ratio.html"
# Content-box dimensions, independently checked against Chromium. Avoid text
# metrics and page coordinates so this also works with different installed fonts.
expected = {
    "block": (300, 200), "from-height": (240, 120), "both": (100, 60),
    "border-box": (176, 76), "content-box": (200, 100),
    "max-width": (120, 60), "max-height": (200, 70),
    "content-min": (100, 150), "min-zero": (100, 100), "scroll": (100, 100),
    "percentage": (200, 50), "flex-a": (150, 75), "flex-b": (150, 75),
    "column": (200, 100), "column-child": (200, 50), "column-content": (100, 150),
    "grid-item": (150, 75), "grid-stretch": (200, 100), "absolute": (200, 100),
    "image-ratio": (100, 100), "image-auto": (100, 50), "image-intrinsic": (200, 200),
    "variable": (150, 100), "border-max-height": (176, 56),
    "flex-transferred": (200, 100), "inline-ratio": (200, 100),
    "grid-container": (200, 100), "absolute-insets": (200, 100),
}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-aspect-ratio-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "1280x900",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in expected:
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True,
                             text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    actual = {item["id"]: item["boxes"][0]["content"] for item in report["elements"]}
    failures = []
    for element_id, size in expected.items():
        rect = actual[element_id]
        measured = rect["width"], rect["height"]
        if measured != size:
            failures.append(f"{element_id}: expected {size}, got {measured}")
    assert not failures, "\n".join(failures)
print(f"Aspect-ratio layout: {len(expected)} geometry checks passed.")
