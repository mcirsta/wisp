#!/usr/bin/env python3
"""Empty blocks must constrain their height before positioning later content."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/empty-min-height.html"
# Border-box heights, independent of font metrics.
heights = {"first": 60, "last": 60, "only": 60, "nested": 60, "margin": 60, "short": 60,
           "max": 40, "conflict": 60, "border-box": 60, "content-box": 80, "percent": 60,
           "indefinite": 0, "calc": 60, "whitespace": 60, "hidden-child": 60, "zero": 0,
           "empty-child": 60, "child-margin": 60}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-empty-min-height-") as tmp:
    dest = Path(tmp)
    args = [str(binary), "--capture-dir", str(dest), "--capture-size", "600x1500",
            "--capture-settle", "50", "--capture-timeout", "10000"]
    for key in heights:
        suffixes = ("", "-case") if key in ("last", "only") else ("", "-case", "-end")
        for suffix in suffixes:
            args.extend(("--capture-id", key + suffix))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert process.returncode == 0, process.stderr
    assert "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((dest / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    inspected = json.loads((dest / "inspection.json").read_text())["elements"]
    assert all(e["status"] == "ok" and e.get("boxes") for e in inspected), inspected
    elements = {e["id"]: e for e in inspected}

    def rect(key):
        assert elements[key]["boxes"], f"Missing box: {key}"
        return elements[key]["boxes"][0]["border_box"]

    for key, height in heights.items():
        b, parent = rect(key), rect(key + "-case")
        assert b["height"] == height, (key, b, height)
        top = {"last": 10, "child-margin": 30, "margin": 15, "empty-child": 15, "zero": 15}.get(key, 0)
        assert b["y"] == parent["y"] + 1 + top, (key, b, parent)
        if key not in ("last", "only"):
            gap = 25 if key in ("margin", "empty-child", "child-margin") else 0
            expected = parent["y"] + 1 + (25 if key == "zero" else top + height + gap)
            assert rect(key + "-end")["y"] == expected, (key, rect(key + "-end"), expected)
        content_height = 200 if key == "percent" else 35 if key == "zero" else height + top
        if key not in ("last", "only", "percent", "zero"):
            content_height += 10 + (25 if key in ("margin", "empty-child", "child-margin") else 0)
        assert parent["height"] == content_height + 2, (key, parent, content_height + 2)
print("Empty block min/max-height: leaf position, ancestors, margins, box sizing and CSS lengths passed.")
