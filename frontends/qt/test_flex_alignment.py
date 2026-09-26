#!/usr/bin/env python3
"""Check flex alignment through CSS parsing, box construction and real layout."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/flex-alignment.html"
# Main-axis position and border-box size, relative to the parent's content box.
# Independently checked against Chromium; there are no network assets or scripts.
expected = {
    "end": ("y", "height", 232, 168),
    "center": ("y", "height", 116, 168),
    "reverse": ("y", "height", 0, 168),
    "edges": ("y", "height", 240, 150),
    "minimum": ("y", "height", 200, 200),
    "background": ("y", "height", 200, 200),
    "auto": ("y", "height", 232, 168),
    "grow": ("y", "height", 0, 400),
    "row": ("x", "width", 232, 168),
    "row-center": ("x", "width", 116, 168),
    "row-reverse": ("x", "width", 0, 168),
}
ids = [item for name in expected for item in (name, name + "-panel")]
ids += [name + "-image" for name in ("background", "auto", "grow")]
ids += [name + "-content" for name in ("minimum", "background")]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-flex-alignment-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "1280x900",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for element_id in ids:
        args.extend(("--capture-id", element_id))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True,
                             text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    boxes = {item["id"]: item["boxes"][0] for item in report["elements"]}
    failures = []
    for name, (axis, size, offset, length) in expected.items():
        parent = boxes[name]["content"]
        child = boxes[name + "-panel"]["border_box"]
        actual = (child[axis] - parent[axis], child[size])
        if actual != (offset, length):
            failures.append(f"{name}: expected {(offset, length)}, got {actual}")
    for name in ("background", "auto", "grow"):
        parent = boxes[name]["content"]
        child = boxes[name + "-image"]["border_box"]
        actual = (child["y"] - parent["y"], child["height"])
        if actual != (0, 400):
            failures.append(f"{name} background: expected {(0, 400)}, got {actual}")
    for name in ("minimum", "background"):
        panel = boxes[name + "-panel"]["content"]
        child = boxes[name + "-content"]["border_box"]
        gap = panel["y"] + panel["height"] - child["y"] - child["height"]
        if gap != 0:
            failures.append(f"{name}: nested end alignment leaves {gap}px")
    assert not failures, "\n".join(failures)
print(f"Flex alignment: {len(expected)} panels, 3 backgrounds and 2 nested children passed.")
