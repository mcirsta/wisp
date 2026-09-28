#!/usr/bin/env python3
"""Generated children share DOM placement rules and retain their formatting."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/generated-boxes.html"
families = ("flex", "grid", "table")
pairs = ("order", "empty", "none", "block", "float", "flex-owner", "grid-owner", "absolute-block", "auto-block") + tuple(
    f"{position}-{family}" for position in ("absolute", "fixed", "auto") for family in families)
ids = [eid for name in pairs for eid in (name, name + "-reference")]
for family in families:
    ids += [f"dom-{family}{suffix}" for suffix in ("", "-target", "-first", "-second", "-suffix")]
    ids += [f"shrink-{family}{suffix}" for suffix in ("-width", "-reference-width")]
for family in ("flow", *families):
    ids += [f"dom-block-{family}{suffix}" for suffix in ("", "-target", "-first", "-second", "-suffix")]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-generated-boxes-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "500x2260",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for eid in ids:
        args += ["--capture-id", eid]
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert process.returncode == 0, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    elements = json.loads((destination / "inspection.json").read_text())["elements"]
    assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}

    def pixels(box):
        geometry = f'{box["width"]}x{box["height"]}+{box["x"]}+{box["y"]}'
        return subprocess.run([magick, str(destination / "page.png"), "-crop", geometry,
                               "+repage", "-depth", "8", "RGB:-"],
                              check=True, capture_output=True, timeout=5).stdout

    for name in pairs:
        actual, reference = boxes[name], boxes[name + "-reference"]
        assert (actual["width"], actual["height"]) == (240, 80), (name, actual)
        assert (reference["width"], reference["height"]) == (240, 80), (name, reference)
        painted = pixels(actual)
        assert painted == pixels(reference), f"{name}: generated content differs from equivalent DOM children"
        if name != "none":
            assert bytes((22, 136, 68)) in painted, f"{name}: generated content is missing"

    # These expectations do not depend on Qt font metrics. Each positioned
    # container follows a 17px inline prefix and must leave the suffix there.
    for family in families:
        name = "dom-" + family
        panel, target, first, second, suffix = (boxes[name + s] for s in
                                               ("", "-target", "-first", "-second", "-suffix"))
        assert target["x"] == panel["x"] + 17 and target["y"] == panel["y"], (name, target, panel)
        assert (target["width"], target["height"]) == (100, 40), (name, target)
        assert suffix["x"] == panel["x"] + 17, (name, suffix)
        assert (first["x"], first["y"]) == (target["x"], target["y"]), (name, first, target)
        spacing = {"flex": 20, "grid": 30, "table": 50}[family]
        assert second["x"] - first["x"] == spacing and second["y"] == first["y"], (name, first, second)
        if family == "table":
            assert first["width"] == second["width"] == 50, (name, first, second)
        actual, reference = (boxes[f"shrink-{family}{s}"] for s in ("-width", "-reference-width"))
        assert (actual["width"], actual["height"]) == (reference["width"], reference["height"]), (
            family, "positioned boxes or trailing spaces changed intrinsic size", actual, reference)
    # Block display values use a hypothetical block position, but must not
    # split the real line or move the following suffix below the prefix.
    for family in ("flow", *families):
        name = "dom-block-" + family
        panel, target, first, second, suffix = (boxes[name + s] for s in
                                               ("", "-target", "-first", "-second", "-suffix"))
        assert target["x"] == panel["x"] and target["y"] == panel["y"] + 20, (name, target, panel)
        assert (suffix["x"], suffix["y"]) == (panel["x"] + 17, panel["y"] + 6), (name, suffix, panel)
        if family == "flow":
            assert second["x"] == first["x"] and second["y"] == first["y"] + 10, (name, first, second)
        else:
            spacing = {"flex": 20, "grid": 30, "table": 50}[family]
            assert second["x"] - first["x"] == spacing and second["y"] == first["y"], (name, first, second)

print("Generated boxes: ordering, empty/hidden content, block/float placement and positioned flex/grid/table passed.")
