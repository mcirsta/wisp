#!/usr/bin/env python3
"""Line-height lengths inherit as computed values; numbers remain multipliers."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/line-height-inheritance.html"
heights = {"em-child": 80, "percent-child": 100, "number-child": 50,
           "tiny-calc-child": 100, "tiny-calc-percent-child": 40,
           "pixels-child": 80, "rem-child": 80, "calc-child": 100,
           "calc-number-child": 50, "fraction-child": 62, "calc-fraction-child": 64,
           "variable-child": 80, "shorthand-child": 80, "grandchild": 80, "override-child": 40,
           "font-prefix-child": 80, "font-variable-child": 80, "font-invalid-prefix-child": 50,
           "font-invalid-size-child": 50, "font-invalid-leading-child": 50, "font-invalid-family-child": 50}
names = (*heights, "normal-child", "normal-control", "viewport-child", "calc-viewport-child",
         "calc-percent-viewport-child", "viewport-font-child", "viewport-font-percent-child",
         "viewport-font-calc-child", "viewport-font-tiny-child")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-line-height-") as tmp:
    dest = Path(tmp)
    args = [str(binary), "--capture-dir", str(dest), "--capture-size", "500x700",
            "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
            "--capture-resize", "700x700", "--capture-resize", "500x700"]
    for name in names:
        args += ["--capture-id", name]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((dest / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((dest / "steps.json").read_text())
    assert len(steps) == 3, len(steps)
    for index, step in enumerate(steps):
        assert all(e["status"] == "ok" and e.get("boxes") for e in step["elements"]), step["elements"]
        boxes = {e["id"]: e["boxes"][0]["content"] for e in step["elements"]}
        for name, height in heights.items():
            assert boxes[name]["height"] == height, (index, name, boxes[name], height)
        assert boxes["normal-child"]["height"] == boxes["normal-control"]["height"], index
        expected_viewport = (100, 140, 100)[index]
        assert boxes["viewport-child"]["height"] == expected_viewport, (index, boxes["viewport-child"])
        assert boxes["calc-viewport-child"]["height"] == 80 + expected_viewport, (index, boxes["calc-viewport-child"])
        assert boxes["calc-percent-viewport-child"]["height"] == 62 + expected_viewport, (
            index, boxes["calc-percent-viewport-child"])
        for name in ("viewport-font-child", "viewport-font-percent-child"):
            assert boxes[name]["height"] == expected_viewport, (index, name, boxes[name])
        for name in ("viewport-font-calc-child", "viewport-font-tiny-child"):
            assert boxes[name]["height"] == expected_viewport + 20, (index, name, boxes[name])
    assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed geometry"

print("Line-height inheritance: lengths, percentages, numbers, calc, variables, shorthand and resize passed.")
