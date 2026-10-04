#!/usr/bin/env python3
"""Empty fragments must not add lines; preserved whitespace and BR still do."""
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
                "pre-space": 20, "atomic": 20, "positioned": 0}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-empty-inline-lines-") as tmp:
    dest = Path(tmp)
    args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x600",
            "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
            "--capture-resize", "500x700", "--capture-resize", "390x600"]
    for name in line_heights:
        args += ["--capture-id", name, "--capture-id", name + "-after"]
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
    assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed geometry"

print("Empty inline lines: nested wrappers, overflow, BR, preserved whitespace and atomic controls passed.")
