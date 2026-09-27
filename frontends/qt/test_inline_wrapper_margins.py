#!/usr/bin/env python3
"""Margin collapsing through actual DOM-to-box inline fragments."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/inline-wrapper-margins.html"
empty = ("plain", "link", "nested", "spaces", "relative", "positioned")
cases = (*empty, "text", "break", "preserved", "inlineblock", "negative")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-inline-margins-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x2000",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in cases:
        for suffix in ("", "-before", "-child", "-after"):
            args.extend(("--capture-id", name + suffix))
    process = subprocess.run([*args, str(fixture)], env=env,
                             capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    for element in report["elements"]:
        assert element["status"] == "ok" and element.get("boxes"), (
            f"Missing rendered box for {element['id']}: {element}")
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}

    def gaps(name):
        before, child, after = (boxes[name + suffix] for suffix in ("-before", "-child", "-after"))
        return (child["y"] - before["y"] - before["height"],
                after["y"] - child["y"] - child["height"])

    for name in empty:
        assert gaps(name) == (13, 17), (name, gaps(name))
        # Three 30px blocks + collapsed gaps 13/17px + two 1px borders.
        assert boxes[name]["height"] == 122, (name, boxes[name])
    assert gaps("negative") == (-13, -17), gaps("negative")
    for name in ("text", "preserved"):
        # Authored 20px line height: 5 + 20 + 13 before, 17 + 20 + 7 after.
        assert gaps(name) == (38, 44), (name, gaps(name))
    # Real line content must continue separating margins. Its baseline/BR
    # height behavior is outside this margin regression.
    for name in ("break", "inlineblock"):
        top, bottom = gaps(name)
        assert top > 18 and bottom > 24, (name, top, bottom)

print("Inline wrapper margins: empty fragments, whitespace, positioned content and line boundaries passed.")
