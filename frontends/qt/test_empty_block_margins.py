#!/usr/bin/env python3
"""Nested empty blocks contribute their collapsed margins exactly once."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/empty-block-margins.html"
# Authored pixel geometry: max(30, 30), 1px padding, nonempty separators,
# and positive/negative collapse after a 40px prefix. No font-dependent values.
heights = {"nested": 30, "bare": 30, "single": 30, "overflow": 30,
           "padded": 31, "nonempty": 62, "mixed": 60, "negative": 20}
lookahead = [(f"lookahead-{sign}-{kind}", margin, kind)
             for sign, margin in (("positive", 150), ("negative", -150))
             for kind in ("leaf", "parent", "first-empty", "trailing-empty")]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-empty-margins-") as tmp:
    destination = Path(tmp)
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "390x800",
            "--capture-settle", "100", "--capture-timeout", "8000",
            "--capture-resize", "500x800", "--capture-resize", "390x800"]
    for name in heights:
        for suffix in ("-column", "-marker"):
            args += ["--capture-id", name + suffix]
    for name, _, kind in lookahead:
        suffixes = ["-case", "-empty", "-inner", "-leaf"]
        if kind != "leaf":
            suffixes.append("-receiver")
        if kind == "first-empty":
            suffixes.append("-inside")
        if kind == "trailing-empty":
            suffixes.append("-prefix")
        for suffix in suffixes:
            args += ["--capture-id", name + suffix]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((destination / "steps.json").read_text())
    assert len(steps) == 3, len(steps)
    for index, step in enumerate(steps):
        elements = step["elements"]
        assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
        boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}
        for name, expected in heights.items():
            column, marker = boxes[name + "-column"], boxes[name + "-marker"]
            assert column["height"] == expected, (index, name, column, expected)
            assert marker["y"] == column["y"] + expected, (index, name, column, marker)
        for name, margin, kind in lookahead:
            case = boxes[name + "-case"]
            top = case["y"] + 1  # Outer border contains the margin chain.
            expected = {"-empty": (top + 200, 0), "-inner": (top + 200, 0),
                        "-leaf": (top + 200 + margin, 10)}
            if kind != "leaf":
                expected["-receiver"] = (top + 200 + margin, 10)
            if kind == "first-empty":
                expected["-inside"] = (top + 200 + margin, 0)
            if kind == "trailing-empty":
                expected["-prefix"] = (top, 200)
            for suffix, geometry in expected.items():
                box = boxes[name + suffix]
                assert (box["y"], box["height"]) == geometry, (index, name + suffix, box, geometry)
            assert case["height"] == 212 + margin, (index, name, case)

print("Empty block margins: nesting, floats, overflow, padding, separators, negative margins "
      "and lookahead receivers passed before and after resizing.")
