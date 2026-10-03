#!/usr/bin/env python3
"""Exercise the UA hidden rule, cascade overrides and native hidden controls."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/hidden-attribute.html"
hidden = ("bare", "empty", "keyword", "mixed-case", "false-value", "invalid", "until-found",
          "parent", "child", "generated", "first", "second", "disabled")
shown = ("visible", "sheet-override", "inline-override")
ids = (*hidden, *shown, "embedded", "embed-sized", "tabs", "first-panel", "second-panel", "after")
actions = ("second", "first", "disabled")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-hidden-attribute-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "400x350",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for element_id in ids:
        args.extend(("--capture-id", element_id))
    for target in actions:
        args.extend(("--capture-click-label", target))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert process.returncode == 0, process.stderr
    assert "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    assert not meta["javascript"], meta
    steps = json.loads((destination / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, len(steps)
    for index, step in enumerate(steps):
        elements = {e["id"]: e for e in step["elements"]}

        def rect(key):
            boxes = elements[key]["boxes"]
            assert boxes, f"Step {index}: missing box for {key}"
            return boxes[0]["border_box"]

        for key in hidden:
            assert not elements[key]["boxes"], f"Step {index}: {key} must have no layout box"
        for key in shown:
            box = rect(key)
            assert (box["width"], box["height"]) == (120, 20), (index, key, box)
        assert rect("visible")["y"] == 0, f"Step {index}: hidden content consumed space"
        embedded = rect("embedded")
        assert (embedded["width"], embedded["height"]) == (0, 0), (index, embedded)
        # Presentational hints override the UA dimensions, as in Chromium.
        sized = rect("embed-sized")
        assert (sized["width"], sized["height"]) == (40, 30), (index, sized)
        selected = "second" if index == 1 else "first"
        other = "first" if selected == "second" else "second"
        assert elements[selected]["state"]["checked"], (index, selected)
        assert not elements[other]["state"]["checked"], (index, other)
        assert not elements["disabled"]["state"]["checked"], index
        assert not elements[other + "-panel"]["boxes"], (index, other)
        height = 50 if selected == "second" else 30
        assert rect(selected + "-panel")["height"] == height, (index, selected)
        tabs = rect("tabs")
        assert tabs["height"] == 40 + height, (index, tabs)
        assert rect("after")["y"] == tabs["y"] + 40 + height, (index, rect("after"))
print("HTML hidden: defaults, author overrides, hidden descendants and native radio labels passed.")
