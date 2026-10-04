#!/usr/bin/env python3
"""Grid intrinsic constraints, outer edges, flex shrinking and native restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Chromium at 96 DPI with the bundled font, before/after changing constraints.
# (case height, line width/height, grid x/y/width/height, peer x/y), relative to case.
CASES = {
    "empty-fixed": (
        (40, 85, 40, 0, 0, 40, 0, 45, 0),
        (40, 135, 40, 0, 0, 90, 0, 95, 0)),
    "fixed-overflow": (
        (40, 85, 40, 0, 0, 40, 10, 45, 0),
        (40, 135, 40, 0, 0, 90, 10, 95, 0)),
    "auto-content": (
        (40, 85, 40, 0, 0, 40, 10, 45, 0),
        (40, 135, 40, 0, 0, 90, 10, 95, 0)),
    "content-box": (
        (40, 109, 40, 7, 0, 50, 10, 69, 0),
        (40, 159, 40, 7, 0, 100, 10, 119, 0)),
    "border-box": (
        (40, 99, 40, 7, 0, 40, 10, 59, 0),
        (40, 149, 40, 7, 0, 90, 10, 109, 0)),
    "border-floor": (
        (40, 55, 40, 0, 0, 10, 10, 15, 0),
        (40, 135, 40, 0, 0, 90, 10, 95, 0)),
    "auto-edges": (
        (40, 109, 40, 7, 0, 50, 20, 69, 0),
        (40, 159, 40, 7, 0, 100, 20, 119, 0)),
    "minimum": (
        (40, 105, 40, 0, 0, 60, 0, 65, 0),
        (40, 105, 40, 0, 0, 60, 0, 65, 0)),
    "maximum": (
        (40, 105, 40, 0, 0, 60, 10, 65, 0),
        (40, 135, 40, 0, 0, 90, 10, 95, 0)),
    "min-wins": (
        (40, 125, 40, 0, 0, 80, 0, 85, 0),
        (40, 125, 40, 0, 0, 80, 0, 85, 0)),
    "fixed-min-wins": (
        (40, 125, 40, 0, 0, 80, 0, 85, 0),
        (40, 125, 40, 0, 0, 80, 0, 85, 0)),
    "fixed-maximum": (
        (40, 85, 40, 0, 0, 40, 0, 45, 0),
        (40, 135, 40, 0, 0, 90, 0, 95, 0)),
    "border-minimum": (
        (40, 105, 40, 0, 0, 60, 10, 65, 0),
        (40, 105, 40, 0, 0, 60, 10, 65, 0)),
    "border-maximum": (
        (40, 105, 40, 0, 0, 60, 20, 65, 0),
        (40, 135, 40, 0, 0, 90, 20, 95, 0)),
    "border-min-wins": (
        (40, 125, 40, 0, 0, 80, 10, 85, 0),
        (40, 125, 40, 0, 0, 80, 10, 85, 0)),
    "border-content-min": (
        (40, 110, 40, 0, 0, 65, 20, 70, 0),
        (40, 110, 40, 0, 0, 65, 20, 70, 0)),
    "border-content-max": (
        (40, 105, 40, 0, 0, 60, 20, 65, 0),
        (40, 135, 40, 0, 0, 90, 20, 95, 0)),
    "calc": (
        (40, 85, 40, 0, 0, 40, 0, 45, 0),
        (40, 135, 40, 0, 0, 90, 0, 95, 0)),
    "auto-margins": (
        (40, 85, 40, 0, 0, 40, 0, 45, 0),
        (40, 135, 40, 0, 0, 90, 0, 95, 0)),
    "tracks": (
        (40, 105, 40, 0, 0, 60, 0, 65, 0),
        (40, 135, 40, 0, 0, 90, 0, 95, 0)),
    "tracks-edges": (
        (40, 115, 40, 0, 0, 70, 10, 75, 0),
        (40, 145, 40, 0, 0, 100, 10, 105, 0)),
    "nested": (
        (40, 85, 40, 0, 0, 40, 10, 45, 0),
        (40, 135, 40, 0, 0, 90, 10, 95, 0)),
    "block-grid": (
        (40, 40, 40, 0, 0, 40, 0, 0, 0),
        (40, 90, 40, 0, 0, 90, 0, 0, 0)),
    "float-grid": (
        (40, 80, 40, 0, 0, 40, 10, 40, 0),
        (40, 130, 40, 0, 0, 90, 10, 90, 0)),
    "flex-shrink": (
        (10, 160, 10, 0, 0, 120, 10, 120, 0),
        (10, 160, 10, 0, 0, 90, 10, 90, 0)),
    "flex-border": (
        (20, 160, 20, 0, 0, 120, 20, 120, 0),
        (20, 160, 20, 0, 0, 90, 20, 90, 0)),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/grid-intrinsic.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"] + [name + "-" + part for name in CASES for part in ("case", "line", "grid", "peer")]
    with tempfile.TemporaryDirectory(prefix="wisp-grid-intrinsic-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "400x1500",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "300x1500", "--capture-resize", "400x1500",
                 "--capture-click-id", "switch", "--capture-click-id", "switch", fixture.as_uri()]
        process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=35)
        assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((destination / "steps.json").read_text())
        assert len(steps) == 5, len(steps)
        for index, report in enumerate(steps):
            elements = {e["id"]: e for e in report["elements"]}
            for key in ids:
                assert elements[key]["status"] == "ok" and elements[key].get("boxes"), (index, key, elements[key])
            checked = index == 3
            assert elements["switch"]["state"]["checked"] == checked, (index, elements["switch"])
            boxes = {key: e["boxes"][0]["border_box"] for key, e in elements.items()}
            next_y = 20
            for name, states in CASES.items():
                ch, lw, lh, gx, gy, gw, gh, px, py = states[int(checked)]
                origin = boxes[name + "-case"]
                assert (origin["x"], origin["y"], origin["height"]) == (20, next_y, ch), (
                    index, name, origin, next_y, ch)
                next_y += ch + 8
                for part, wanted in (("line", (0, 0, lw, lh)), ("grid", (gx, gy, gw, gh)), ("peer", (px, py, 40, 10))):
                    box = boxes[name + "-" + part]
                    actual = (box["x"] - origin["x"], box["y"] - origin["y"], box["width"], box["height"])
                    assert actual == wanted, (index, name, part, actual, wanted)
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"Grid intrinsic sizes: {len(CASES)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
