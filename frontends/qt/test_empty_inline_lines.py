#!/usr/bin/env python3
"""Empty fragments collapse unless horizontal edges create a real line."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/empty-inline-lines.html"
# Explicit 20px line-height and bundled font: the 240px word overflows a
# 200px container but occupies just one line, regardless of nested spans.
line_heights = {"empty": 0, "collapsed": 0, "oversize": 20, "break": 20,
                "two-breaks": 40, "pre-newline": 20, "pre-two-newlines": 40,
                "pre-space": 20, "atomic": 20, "positioned": 0,
                "split-left": 32, "split-right": 32, "split-vertical": 12, "split-both": 52,
                "nested-large": 40, "empty-large": 40, "parent-zero": 20}
for name in ("padding-left", "padding-right", "border-left", "border-right", "margin-left",
             "margin-right", "margin-negative", "calc-nonzero", "percent", "small", "zero-line", "zero-font"):
    line_heights["edge-" + name] = 20
for name in ("padding-top", "padding-bottom", "border-top", "border-bottom", "margin-top",
             "margin-bottom", "border-hidden", "border-none", "border-zero", "calc-zero"):
    line_heights["edge-" + name] = 0
for name in ("big", "tall-line", "top", "bottom"):
    line_heights["edge-" + name] = 40
# 40px font with a 20px line: 22px ascent, -2px descent, plus the parent's
# 4px descent. Borders/padding themselves must not inflate line height.
line_heights["edge-big-tight"] = 26
split_child_y = {"split-left": 20, "split-right": 0, "split-vertical": 0, "split-both": 20}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-empty-inline-lines-") as tmp:
    dest = Path(tmp)
    args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x600",
            "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
            "--capture-resize", "500x700", "--capture-resize", "390x600"]
    for name in line_heights:
        args += ["--capture-id", name, "--capture-id", name + "-after"]
    for name in split_child_y:
        args += ["--capture-id", name + "-child"]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((dest / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((dest / "steps.json").read_text())
    assert len(steps) == 3, len(steps)
    for index, step in enumerate(steps):
        assert all(e["status"] == "ok" and e.get("boxes") for e in step["elements"]), step["elements"]
        boxes = {e["id"]: e["boxes"][0]["border_box"] for e in step["elements"]}
        for name, height in line_heights.items():
            case, after = boxes[name], boxes[name + "-after"]
            assert after["y"] - case["y"] - 1 == height, (index, name, after, case, height)
            assert case["height"] == height + 10, (index, name, case, height)
        for name, y in split_child_y.items():
            actual = boxes[name + "-child"]["y"] - boxes[name]["y"] - 1
            assert actual == y, (index, name, actual, y)
    assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed geometry"

print(f"Empty inline lines: {len(line_heights)} fragment, edge, font and whitespace cases passed after resizing.")
