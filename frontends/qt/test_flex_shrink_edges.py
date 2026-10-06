#!/usr/bin/env python3
"""Flexbox 9.7: scale shrink by inner bases, preserving fixed outer edges."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/flex-shrink-edges.html"
# Content sizes for the 500px and 600px viewports. Two equal 100px bases
# absorb equal losses, except for explicit factors and min/max constraints.
sizes = {
    "control": ((50, 50), (100, 100)),
    "margin": ((50, 50), (100, 100)),
    "padding": ((50, 50), (100, 100)),
    "border": ((50, 50), (100, 100)),
    "border-box": ((50, 50), (100, 100)),
    "auto-basis": ((50, 50), (100, 100)),
    "weighted": ((25, 75), (100, 100)),
    "minimum": ((80, 20), (100, 100)),
    "maximum": ((60, 80), (60, 100)),
    "inflexible": ((100, 0), (100, 100)),
    "negative": ((70, 70), (100, 100)),
    "zero": ((0, 50), (0, 100)),
    "all-zero": ((0, 0), (0, 0)),
    "growth": ((150, 150), (200, 200)),
}
directions = ("row", "row-reverse", "column", "column-reverse")
names = [f"{direction}-{case}" for direction in directions for case in sizes]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-flex-shrink-") as tmp:
    dest = Path(tmp)
    args = [str(binary), "--capture-dir", str(dest), "--capture-size", "500x800",
            "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
            "--capture-resize", "600x800", "--capture-resize", "500x800"]
    for name in names:
        for suffix in ("", "-a", "-b"):
            args += ["--capture-id", name + suffix]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((dest / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((dest / "steps.json").read_text())
    assert len(steps) == 3, len(steps)
    for index, step in enumerate(steps):
        assert all(e["status"] == "ok" and e.get("boxes") for e in step["elements"]), step["elements"]
        boxes = {e["id"]: e["boxes"][0] for e in step["elements"]}
        for direction in directions:
            dimension, coordinate = ("width", "x") if direction.startswith("row") else ("height", "y")
            for case, expected in sizes.items():
                name = f"{direction}-{case}"
                parent, first, second = (boxes[name + suffix] for suffix in ("", "-a", "-b"))
                actual = (first["content"][dimension], second["content"][dimension])
                assert actual == expected[index == 1], (index, name, actual, expected[index == 1])
                # Padding/border remain fixed, including for border-box bases.
                border_edge = 100 if case in ("padding", "border", "border-box", "minimum", "maximum") else 0
                assert first["border_box"][dimension] == actual[0] + border_edge, (index, name, first)
                assert second["border_box"][dimension] == actual[1], (index, name, second)
                margin = -40 if case == "negative" else 100 if case in (
                    "margin", "auto-basis", "weighted", "inflexible", "zero", "all-zero", "growth") else 0
                outer_first = actual[0] + border_edge + margin
                if direction.endswith("reverse"):
                    first_pos = parent["content"][dimension] - outer_first
                    second_pos = first_pos - actual[1] - (100 if case == "all-zero" else 0)
                else:
                    first_pos, second_pos = 0, outer_first
                positions = tuple(box["border_box"][coordinate] - parent["content"][coordinate]
                                  for box in (first, second))
                assert positions == (first_pos, second_pos), (index, name, positions, (first_pos, second_pos))
    assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed flex geometry"

print(f"Flex shrink: {len(names)} cases passed in all four directions, including a resize round trip.")
