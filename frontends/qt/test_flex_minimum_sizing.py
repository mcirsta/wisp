#!/usr/bin/env python3
"""Flex content minimums, preferred-size contributions and cache invalidation."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/flex-minimum-sizing.html"
# Content widths checked against Chromium. Text cases deliberately use very
# short/long words so exact font metrics cannot change the expected clamp.
expected = {
    "auto": 600, "zero": 400, "calc": 400, "variable": 400,
    "specified": 400, "capped": 250, "hidden": 400,
    "main": 600, "news": 430, "sidebar": 150,
    "empty": 50, "short-text": 50, "long-text": 80, "long-peer": 20,
    "inline-text": 80, "nested-block": 60, "nested-peer": 40,
    "nested-flex": 55, "content-edges": 20, "border-edges": 20,
    "explicit-min": 60, "explicit-zero": 50, "maximum": 25, "maximum-peer": 75,
    "gap": 40, "no-shrink": 80, "shrinking-peer": 20, "basis": 50,
    "out-of-flow": 50, "ratio": 60, "intrinsic-row": 160,
    "dynamic": 50, "dynamic-peer": 50,
}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
actions = [("click-label", "grow"), ("click-label", "grow"),
           ("resize", "640x2400"), ("click-label", "grow"), ("click-label", "grow")]


def capture(destination, flags=()):
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x2400",
            "--capture-settle", "50", "--capture-timeout", "10000", *flags]
    for key in expected:
        args += ["--capture-id", key]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0 and "runtime error:" not in result.stderr, result.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((destination / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, len(steps)
    geometries = []
    for index, step in enumerate(steps):
        boxes = {}
        for element in step["elements"]:
            assert element["boxes"], f"Step {index}: missing box for {element['id']}"
            boxes[element["id"]] = element["boxes"][0]["content"]
        widths = dict(expected)
        if index in (1, 4):
            widths.update(dynamic=60, **{"dynamic-peer": 40})
        failures = [f"step {index}, {key}: expected {width}, got {boxes[key]['width']}"
                    for key, width in widths.items() if boxes[key]["width"] != width]
        assert not failures, "\n".join(failures)
        geometries.append(boxes)
    return geometries


with tempfile.TemporaryDirectory(prefix="wisp-flex-minimum-") as tmp:
    tmp = Path(tmp)
    reference = capture(tmp / "full", ("--capture-force-full-restyle",))
    assert capture(tmp / "retained") == reference
print(f"Flex minimum sizing: {len(expected)} widths, updates, resize and retained/full parity passed.")
