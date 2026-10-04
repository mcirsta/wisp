#!/usr/bin/env python3
"""Grid container heights, gap axes, percentages, resizing and native restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Chromium at 96 DPI with the bundled font, unchecked/checked.
# Each tuple is (case height, grid x/y/width/height relative to its case).
CASES = {
    "empty": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "overflow": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "zero": ((0, 0, 0, 100, 0), (20, 0, 0, 100, 20)),
    "auto": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "minimum": ((40, 0, 0, 100, 40), (20, 0, 0, 100, 20)),
    "maximum": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "min-wins": ((40, 0, 0, 100, 40), (20, 0, 0, 100, 20)),
    "border-height": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "border-floor": ((10, 0, 0, 100, 10), (20, 0, 0, 100, 20)),
    "border-min": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "border-max": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "percent": ((100, 0, 0, 100, 50), (100, 0, 0, 100, 20)),
    "percent-limits": ((100, 0, 0, 100, 60), (100, 0, 0, 100, 20)),
    "indefinite": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "calc": ((100, 0, 0, 100, 40), (100, 0, 0, 100, 20)),
    "percent-padding": ((80, 0, 0, 100, 80), (40, 0, 0, 100, 40)),
    "inline": ((35, 0, 0, 100, 30), (25, 0, 0, 100, 20)),
    "float": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "absolute": ((50, 0, 0, 100, 30), (50, 0, 0, 100, 20)),
    "absolute-percent": ((120, 0, 0, 100, 60), (120, 0, 0, 100, 20)),
    "ratio": ((50, 0, 0, 100, 50), (20, 0, 0, 100, 20)),
    "ratio-content": ((80, 0, 0, 100, 80), (20, 0, 0, 100, 20)),
    "ratio-minzero": ((50, 0, 0, 100, 50), (20, 0, 0, 100, 20)),
    "ratio-max": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "nested": ((60, 0, 0, 100, 60), (20, 0, 0, 100, 20)),
    "flex-stretch": ((60, 0, 0, 100, 60), (60, 0, 0, 100, 20)),
    "flex-column": ((40, 0, 0, 100, 40), (40, 0, 0, 100, 20)),
    "grid-stretch": ((60, 0, 0, 100, 60), (60, 0, 0, 100, 20)),
    "rows": ((38, 0, 0, 100, 38), (20, 0, 0, 100, 20)),
    "row-percent-auto": ((30, 0, 0, 100, 30), (20, 0, 0, 100, 20)),
    "row-percent-fixed": ((100, 0, 0, 100, 100), (20, 0, 0, 100, 20)),
    "row-calc": ((38, 0, 0, 100, 38), (20, 0, 0, 100, 20)),
    "two-axis": ((28, 0, 0, 100, 28), (20, 0, 0, 100, 20)),
    "resize": ((110, 0, 0, 220, 110), (20, 0, 0, 220, 20)),
    "flex-padding": ((80, 0, 0, 100, 80), (40, 0, 0, 100, 40)),
    "nested-padding": ((80, 0, 0, 100, 80), (40, 0, 0, 100, 40)),
}

# Only the responsive ratio case changes geometry at the narrower viewport.
NARROW = {
    "resize": (90, 0, 0, 180, 90),
}

# Additional descendant border boxes, relative to their enclosing grid.
EXTRA = {
    "nested-inner": ((0, 0, 100, 20), (0, 0, 100, 20)),
    "rows-second": ((0, 18, 100, 20), (0, 18, 100, 20)),
    "row-percent-auto-second": ((0, 13, 100, 20), (0, 12, 100, 20)),
    "row-percent-fixed-second": ((0, 20, 100, 20), (0, 12, 100, 20)),
    "row-calc-second": ((0, 18, 100, 20), (0, 18, 100, 20)),
    "two-axis-second": ((43, 0, 40, 10), (43, 0, 40, 10)),
    "two-axis-third": ((0, 18, 40, 10), (0, 18, 40, 10)),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/grid-height.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"] + [name + "-" + part for name in CASES for part in ("case", "grid")] + list(EXTRA)
    with tempfile.TemporaryDirectory(prefix="wisp-grid-height-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "400x6000",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "360x6000", "--capture-resize", "400x6000",
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

            def check(key, origin, wanted):
                box = boxes[key]
                actual = (box["x"] - origin["x"], box["y"] - origin["y"], box["width"], box["height"])
                assert actual == tuple(wanted), (index, key, actual, wanted)

            next_y = 20
            for name, states in CASES.items():
                height, *wanted = NARROW.get(name, states[0]) if index == 1 else states[int(checked)]
                origin = boxes[name + "-case"]
                assert (origin["x"], origin["y"], origin["height"]) == (20, next_y, height), (
                    index, name, origin, next_y, height)
                next_y += height + 80
                check(name + "-grid", origin, wanted)
            for key, states in EXTRA.items():
                check(key, boxes[key.rsplit("-", 1)[0] + "-grid"], states[int(checked)])
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"Grid heights: {len(CASES)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
