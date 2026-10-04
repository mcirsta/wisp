#!/usr/bin/env python3
"""Marker-only line sizing, adjoining margins, floats and repeated native restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/empty-marker-lines.html"
names = ("empty", "small", "float", "abs", "spaces", "fixed", "min", "max", "none", "zero",
         "padded", "flowroot", "image", "negative", "removed")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")


def box(step, name):
    boxes = step[name]["boxes"]
    assert len(boxes) == 1, (name, step[name])
    return boxes[0]


with tempfile.TemporaryDirectory(prefix="wisp-empty-markers-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x1800",
            "--capture-settle", "100", "--capture-timeout", "12000", "--enable_javascript=0"]
    for name in names:
        for key in (name, name + "-case", name + "-tail"):
            args += ["--capture-id", key]
    for key in ("switch", "floated", "positioned", "contained-float", "atomic", "adjacent", "inline-case"):
        args += ["--capture-id", key]
    args += ["--capture-resize", "500x1800", "--capture-resize", "700x1800",
             "--capture-click-id", "switch", "--capture-click-id", "switch"]
    process = subprocess.run([*args, fixture.as_uri()], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = [{e["id"]: e for e in step["elements"]}
             for step in json.loads((destination / "steps.json").read_text())]
    assert len(steps) == 5, len(steps)
    for index, step in enumerate(steps):
        changed = index == 3
        assert step["switch"]["state"]["checked"] == changed, (index, step["switch"])
        heights = dict.fromkeys(names, 36 if changed else 42)
        heights.update(small=36, fixed=10, min=60, max=10, none=0, zero=0 if changed else 6,
                       padded=52 if changed else 58, flowroot=48 if changed else 54, image=52)
        if changed:
            heights["removed"] = 0
        for name, expected in heights.items():
            item = box(step, name)["border_box"]
            case = box(step, name + "-case")["border_box"]
            tail = box(step, name + "-tail")["border_box"]
            assert item["height"] == expected, (index, name, item, expected)
            no_marker = name == "none" or (name == "removed" and changed)
            top, bottom = (-13, -17) if name == "negative" else (13, 17)
            # Without a marker, the empty item's own margins adjoin (17px).
            # Even a zero-height marker line keeps them separate (13 + 17px).
            offset = 17 if no_marker else top
            following = 17 if no_marker else top + expected + bottom
            assert item["y"] - case["y"] == offset, (index, name, item, case, offset)
            assert tail["y"] - case["y"] == following, (index, name, tail, case, following)
        push = 0 if changed else 6
        atomic = box(step, "atomic")["border_box"]
        adjacent = box(step, "adjacent")["border_box"]
        assert atomic["height"] == 36 + push, (index, atomic)
        assert adjacent["y"] == atomic["y"] + push, (index, atomic, adjacent)
        assert box(step, "inline-case")["border_box"]["height"] == 36 + push, (index, step["inline-case"])
        for child, parent in (("floated", "float"), ("positioned", "abs"), ("contained-float", "flowroot")):
            assert box(step, child)["border_box"]["y"] == box(step, parent)["border_box"]["y"] + push, (
                index, child, box(step, child), box(step, parent))
    for name in names:
        for key in ("border_box", "list_marker"):
            assert box(steps[0], name)[key] == box(steps[-1], name)[key], (name, key)
print("Empty markers: line sizing, margins, CSS constraints, floats, images, resize and native restyles passed.")
