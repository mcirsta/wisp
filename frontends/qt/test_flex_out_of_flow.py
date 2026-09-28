#!/usr/bin/env python3
"""Check flex sizing and independent absolute layout, including auto insets."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Border boxes relative to the flex parent's content box; all lengths are px.
# These expectations are also checked independently against Chromium.
expected = {
    "shrink-item": ("shrink", 0, 0, 400, 40),
    "shrink-overlay": ("shrink", 0, 0, 44, 20),
    "grow-item": ("grow", 0, 0, 400, 40),
    "relative-item": ("relative", 0, 0, 356, 40),
    "relative-overlay": ("relative", 356, 0, 44, 20),
    "reverse-item": ("reverse", 0, 0, 400, 40),
    "column-item": ("column", 0, 0, 400, 200),
    "gap-a": ("gap", 0, 0, 150, 40),
    "gap-b": ("gap", 170, 0, 150, 40),
    "wrap-a": ("wrap", 0, 0, 150, 40),
    "wrap-b": ("wrap", 170, 0, 150, 40),
    "wrap-c": ("wrap", 0, 60, 100, 40),
    "between-a": ("between", 0, 0, 100, 40),
    "between-b": ("between", 300, 0, 100, 40),
    "auto-margin-item": ("auto-margin", 300, 0, 100, 40),
    "empty-overlay": ("empty", 0, 0, 44, 150),
    "intrinsic-item": ("intrinsic", 0, 0, 100, 40),
    "center-overlay": ("center", 180, 30, 40, 20),
    "end-overlay": ("end", 360, 60, 40, 20),
    "static-reverse-overlay": ("static-reverse", 360, 0, 40, 20),
    "static-column-overlay": ("static-column", 180, 80, 40, 20),
    "edges-overlay": ("edges", 175, 25, 50, 30),
    "auto-size-overlay": ("auto-size", 160, 0, 80, 30),
    "auto-size-child": ("auto-size-overlay", 0, 0, 80, 30),
    "fixed-static-overlay": ("fixed-static", 180, 30, 40, 20),
    "ancestor-overlay": ("ancestor", 180, 30, 40, 20),
    "one-inset-overlay": ("one-inset", 7, 30, 40, 20),
}
parent_sizes = {"empty": (400, 0), "minimum": (400, 100), "intrinsic": (100, 40), "wrap": (320, 100)}
ids = sorted(set(expected) | {value[0] for value in expected.values()} | set(parent_sizes) | {"grow-overlay"})


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/flex-out-of-flow.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-flex-outflow-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x2400",
                "--capture-settle", "100", "--capture-timeout", "8000"]
        for element_id in ids:
            args.extend(("--capture-id", element_id))
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        report = json.loads((destination / "inspection.json").read_text())
        for item in report["elements"]:
            assert item["boxes"], f"Missing layout box for {item['id']}"
        boxes = {item["id"]: item["boxes"][0] for item in report["elements"]}
        failures = []
        for name, (parent, *geometry) in expected.items():
            rect = boxes[name]["border_box"]
            origin = boxes[parent]["content"]
            actual = [rect["x"] - origin["x"], rect["y"] - origin["y"], rect["width"], rect["height"]]
            if actual != geometry:
                failures.append(f"{name}: expected {geometry}, got {actual}")
        for name, size in parent_sizes.items():
            rect = boxes[name]["content"]
            actual = rect["width"], rect["height"]
            if actual != size:
                failures.append(f"{name}: expected size {size}, got {actual}")
        rect = boxes["grow-overlay"]["border_box"]
        assert (rect["x"], rect["y"], rect["width"], rect["height"]) == (450, 0, 44, 20), rect
        assert not failures, "\n".join(failures)
    print(f"Out-of-flow flex children: {len(expected) + len(parent_sizes) + 1} geometry checks passed.")


if __name__ == "__main__":
    main()
