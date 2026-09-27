#!/usr/bin/env python3
"""Exercise whitespace through DOM construction, intrinsic sizing and layout."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/inline-spacing.html"
# Expected advance before the 1px marker (the bundled font has exact metrics).
advances = {"nested": 154, "hidden": 154, "flat": 154, "duplicate": 29,
            "empty": 29, "leading": 12, "adjacent": 24, "blocks": 45,
            "flex": 45, "grid": 45, "image": 45, "padding": 36,
            "nowrap": 29, "pre": 34}
cases = (*advances, "break", "wrap", "wrap-blocks")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-inline-spacing-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x900",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in cases:
        for suffix in ("", "-end"):
            args.extend(("--capture-id", name + suffix))
    for name in ("trailing", "wrap-right", "wrap-right-first"):
        args.extend(("--capture-id", name))
    process = subprocess.run([*args, str(fixture)], env=env,
                             capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    for element in report["elements"]:
        assert element["status"] == "ok" and element.get("boxes"), element
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
    for name, advance in advances.items():
        box, end = boxes[name], boxes[name + "-end"]
        assert end["x"] - box["x"] == advance, (name, "advance", box, end)
        assert box["width"] == advance + 1, (name, "intrinsic width", box)
        if name not in ("blocks", "flex", "grid", "image"):
            assert box["height"] == 30, (name, "unexpected wrap", box)
    for name, advance in (("break", 12), ("wrap", 24), ("wrap-blocks", 20)):
        box, end = boxes[name], boxes[name + "-end"]
        assert end["x"] - box["x"] == advance, (name, "line-leading space", box, end)
        assert end["y"] > box["y"], (name, "missing wrap", box, end)
        if name == "wrap":
            assert box["height"] == 60, (name, "line count", box)
    assert boxes["trailing"]["width"] == 12, ("trailing space", boxes["trailing"])
    assert boxes["trailing"]["height"] == 30, ("trailing empty line", boxes["trailing"])
    assert boxes["wrap-right-first"]["x"] - boxes["wrap-right"]["x"] == 6, (
        "trailing space before wrapping", boxes["wrap-right-first"], boxes["wrap-right"])

print("Inline spacing: nested text, collapsed spaces, atomic inlines, intrinsic widths and wrapping passed.")
