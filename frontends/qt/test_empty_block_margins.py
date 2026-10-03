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
positions = {"positive": (75, 110), "negative": (45, 10), "mixed": (57, 95),
             "mixed-negative": (63, 25), "nested-positive": (100, 110),
             "nested-negative": (20, 10), "nested-mixed": (50, 70), "parent": (110, 110)}
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
    for name in positions:
        for suffix in ("-case", "-empty", "-dot", "-after"):
            args += ["--capture-id", "position-" + name + suffix]
        if name.startswith("nested-"):
            args += ["--capture-id", "position-" + name + "-child"]
        if name == "parent":
            args += ["--capture-id", "position-parent-wrapper"]
    for name in ("mixed-second", "negative-second"):
        args += ["--capture-id", name]
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
        for name, (empty_y, following_y) in positions.items():
            key = "position-" + name
            top = boxes[key + "-case"]["y"] + 1
            for suffix, y, height in (("-empty", empty_y, 0), ("-dot", empty_y, 3),
                                       ("-after", following_y, 10)):
                box = boxes[key + suffix]
                assert (box["y"] - top, box["height"]) == (y, height), (index, key + suffix, box, top, y)
            assert boxes[key + "-case"]["height"] == following_y + 12, (index, key, boxes[key + "-case"])
            if name.startswith("nested-"):
                assert boxes[key + "-child"]["y"] == top + empty_y, (index, key, boxes[key + "-child"])
            if name == "parent":
                assert boxes[key + "-wrapper"]["y"] == top + empty_y, (index, key, boxes[key + "-wrapper"])
        for name, offset in (("mixed", 70), ("negative", 30)):
            assert boxes[name + "-second"]["y"] == boxes[name + "-column"]["y"] + offset, (index, name, boxes[name + "-second"])

print("Empty block margins: nesting, floats, overflow, padding, separators, negative margins "
      "and lookahead receivers passed before and after resizing.")
