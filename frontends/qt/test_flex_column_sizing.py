#!/usr/bin/env python3
"""Column shrink sizes must survive nested layout and width stretching."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Border-box top and height relative to the parent's content box, in px.
# These expectations are also checked independently against Chromium.
expected = {
    "basic-item": ("basic", 0, 200),
    "overlay-item": ("overlay", 0, 200),
    "reverse-a": ("reverse", 50, 150), "reverse-b": ("reverse", 0, 50),
    "proportional-a": ("proportional", 0, 150), "proportional-b": ("proportional", 150, 50),
    "minimum-a": ("minimum", 0, 180), "minimum-b": ("minimum", 180, 20),
    "inflexible-a": ("inflexible", 0, 100), "inflexible-b": ("inflexible", 100, 100),
    "weighted-a": ("weighted", 0, 200), "weighted-b": ("weighted", 200, 0),
    "edges-item": ("edges", 5, 190),
    "gap-a": ("gap", 0, 150), "gap-b": ("gap", 170, 50),
    "nested-item": ("nested", 0, 200), "nested-half": ("nested-item", 0, 100),
    "nested-flex-item": ("nested-flex", 0, 200), "nested-flex-half": ("nested-flex-item", 0, 100),
    "nested-grid-item": ("nested-grid", 0, 200), "nested-grid-child": ("nested-grid-item", 0, 400),
    "ratio-item": ("ratio", 0, 200), "ratio-half": ("ratio-item", 0, 100),
    "content-item": ("content", 0, 200), "content-child": ("content-item", 0, 400),
    "auto-minimum-item": ("auto-minimum", 0, 300),
    "overflow-a": ("overflow", 0, 240), "overflow-b": ("overflow", 240, 0),
    "zero-item": ("zero", 0, 0),
    "indefinite-item": ("indefinite", 0, 20), "indefinite-half": ("indefinite-item", 0, 20),
    "indefinite-basis-item": ("indefinite-basis", 0, 20),
    "indefinite-basis-half": ("indefinite-basis-item", 0, 20),
    "indefinite-calc-item": ("indefinite-calc", 0, 20),
    "indefinite-calc-half": ("indefinite-calc-item", 0, 20),
    "growth-item": ("growth", 0, 200), "growth-half": ("growth-item", 0, 100),
}
ids = sorted(set(expected) | {value[0] for value in expected.values()})


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/flex-column-sizing.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-flex-column-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x4300",
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
            actual = [rect["y"] - origin["y"], rect["height"]]
            if actual != geometry:
                failures.append(f"{name}: expected {geometry}, got {actual}")
        assert not failures, "\n".join(failures)
    print(f"Column flex sizing: {len(expected)} geometry checks passed.")


if __name__ == "__main__":
    main()
