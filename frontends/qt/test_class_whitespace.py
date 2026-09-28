#!/usr/bin/env python3
"""Class whitespace must work through HTML parsing, CSS selection and layout."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/class-whitespace.html"
expected = {name: (200, 30) for name in ("space", "newline", "tab", "formfeed", "carriage", "crlf")}
expected.update({"mixed": (180, 30), "newline-wide": (100, 30)})
expected.update({name: (240, 10) for name in ("vertical-tab", "nbsp", "unicode-space")})
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-class-whitespace-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "400x400",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in expected:
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    elements = {item["id"]: item for item in report["elements"]}
    for element_id, size in expected.items():
        boxes = elements[element_id]["boxes"]
        assert boxes, f"Missing layout box for {element_id}"
        rect = boxes[0]["content"]
        actual = rect["width"], rect["height"]
        assert actual == size, f"{element_id}: expected {size}, got {actual}"
print(f"Class whitespace: {len(expected)} geometry checks passed.")
