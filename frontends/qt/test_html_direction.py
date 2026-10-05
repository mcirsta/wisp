#!/usr/bin/env python3
"""HTML dir defaults, semantic :dir(), CSS cascade and inherited layout direction.

Expected directions follow HTML's rendering rules; alignment and column order
follow CSS 2.2, without a browser snapshot as the oracle. This does not test bidi
reordering or live editing direction changes, which remain separate work.

https://html.spec.whatwg.org/multipage/rendering.html#bidirectional-text
https://www.w3.org/TR/CSS22/text.html#alignment-prop
https://www.w3.org/TR/CSS22/tables.html#table-layout
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/html-direction.html"
directions = dict(ltr="ltr", rtl="rtl", upper="rtl", mixed="rtl", sheet="ltr", inline="ltr",
                  revert="rtl", padded="ltr", inherit="rtl", reset="ltr", invalid="rtl", empty="rtl", live="rtl")
directions.update({"auto-rtl": "rtl", "auto-ltr": "ltr", "auto-excluded": "ltr", "bdi-auto": "rtl",
                   "semantic-reset": "rtl", "css-inherit": "ltr", "auto-neutral": "ltr"})
semantic = {"auto-rtl": "rtl", "auto-ltr": "ltr", "auto-excluded": "ltr", "bdi-auto": "rtl",
            "semantic-reset": "rtl", "css-inherit": "rtl", "auto-neutral": "ltr",
            "telephone": "ltr", "auto-input": "rtl", "auto-textarea": "rtl"}
ids = [key for name in directions for key in (name, name + "-chip")]
ids += ["telephone", "auto-input", "auto-textarea"]
ids += ["table", "table-a", "table-b", "table-override", "override-a", "override-b"]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-html-direction-") as tmp:
    output = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(output), "--capture-size", "500x600",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    args += ["--capture-resize", "400x600", "--capture-click-id", "switch-label",
             "--capture-click-id", "switch-label", str(fixture)]
    run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=25)
    assert run.returncode == 0 and "runtime error:" not in run.stderr, run.stderr
    meta = json.loads((output / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((output / "steps.json").read_text())
    assert len(steps) == 4, len(steps)
    for index, step in enumerate(steps):
        elements = {e["id"]: e for e in step["elements"]}

        def box(key):
            assert elements[key]["boxes"], (index, key)
            return elements[key]["boxes"][0]

        def rect(key):
            return box(key)["border_box"]

        for key, direction in directions.items():
            if key == "live" and index == 2:
                direction = "ltr"
            for target in (key, key + "-chip"):
                assert f"direction: {direction} " in box(target)["computed_style_dump"], (index, target, direction)
            parent, child = rect(key), rect(key + "-chip")
            expected_x = parent["width"] - child["width"] if direction == "rtl" else 0
            assert child["x"] - parent["x"] == expected_x, (index, key, parent, child)
        for key, direction in semantic.items():
            dump = box(key)["computed_style_dump"]
            color = "#ff654321" if direction == "rtl" else "#ff123456"
            assert f"color: {color}" in dump, (index, key, direction, dump)
            if key not in directions:
                assert f"direction: {direction} " in dump, (index, key, direction)
        # Row/cell direction cannot change the direction of the table's columns.
        for table, a, b, rtl in (("table", "table-a", "table-b", True),
                                  ("table-override", "override-a", "override-b", False)):
            parent, first, second = rect(table), rect(a), rect(b)
            assert first["width"] == 20 and second["width"] == 40, (index, table, first, second)
            assert first["x"] - parent["x"] == (40 if rtl else 0), (index, table, first)
            assert second["x"] - parent["x"] == (0 if rtl else 20), (index, table, second)
print("HTML direction: keywords, auto, bdi, semantic selectors, CSS overrides, columns and native restyles passed.")
