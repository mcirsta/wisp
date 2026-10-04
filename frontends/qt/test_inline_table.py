#!/usr/bin/env python3
"""Inline tables: Chromium geometry, resize and native display-change restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Relative border boxes from Chromium at 96 DPI using test/fonts/normal-400.ttf.
# None widths are viewport-dependent, or the opening fragment of an inline
# whose content is represented by separate text/end boxes in Wisp's inspector.
CASES = {

    "basic": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "top": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 40, None, 8),
        "table": (40, 0, 120, 40),
        "cell0": (40, 0, 120, 20),
        "content0": (40, 0, 120, 20),
        "cell1": (40, 20, 120, 20),
        "content1": (40, 20, 120, 20),
    },
    "bottom": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 40, None, 8),
        "table": (40, 0, 120, 40),
        "cell0": (40, 0, 120, 20),
        "content0": (40, 0, 120, 20),
        "cell1": (40, 20, 120, 20),
        "content1": (40, 20, 120, 20),
    },
    "middle": {
        "case": (0, 0, None, 53),
        "line": (0, 0, 300, 45),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 45, None, 8),
        "table": (40, 5, 120, 40),
        "cell0": (40, 5, 120, 20),
        "content0": (40, 5, 120, 20),
        "cell1": (40, 25, 120, 20),
        "content1": (40, 25, 120, 20),
    },
    "edges": {
        "case": (0, 0, None, 89),
        "line": (0, 0, 300, 81),
        "before": (0, 1, 40, 40),
        "after-peer": (188, 1, 40, 40),
        "after": (0, 81, None, 8),
        "table": (50, 7, 130, 65),
        "cell0": (59, 17, 112, 20),
        "content0": (59, 17, 112, 20),
        "cell1": (59, 42, 112, 20),
        "content1": (59, 42, 112, 20),
    },
    "border-box": {
        "case": (0, 0, None, 79),
        "line": (0, 0, 300, 71),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 71, None, 8),
        "table": (40, 6, 120, 65),
        "cell0": (49, 16, 102, 20),
        "content0": (49, 16, 102, 20),
        "cell1": (49, 41, 102, 20),
        "content1": (49, 41, 102, 20),
    },
    "block-edges": {
        "case": (0, 0, None, 153),
        "line": (0, 0, 300, 145),
        "before": (0, 0, 40, 40),
        "after-peer": (0, 105, 40, 40),
        "after": (0, 145, None, 8),
        "table": (0, 40, 130, 65),
        "cell0": (9, 50, 112, 20),
        "content0": (9, 50, 112, 20),
        "cell1": (9, 75, 112, 20),
        "content1": (9, 75, 112, 20),
    },
    "intrinsic-edges": {
        "case": (0, 0, None, 79),
        "line": (0, 0, 300, 71),
        "before": (0, 0, 40, 40),
        "after-peer": (106, 0, 40, 40),
        "after": (0, 71, None, 8),
        "table": (40, 6, 66, 65),
        "cell0": (49, 16, 48, 20),
        "content0": (49, 16, 48, 20),
        "cell1": (49, 41, 48, 20),
        "content1": (49, 41, 48, 20),
    },
    "shrink-host": {
        "case": (0, 0, None, 79),
        "line": (0, 0, 210, 71),
        "before": (0, 0, 40, 40),
        "after-peer": (170, 0, 40, 40),
        "after": (0, 71, None, 8),
        "table": (40, 6, 130, 65),
        "cell0": (49, 16, 112, 20),
        "content0": (49, 16, 112, 20),
        "cell1": (49, 41, 112, 20),
        "content1": (49, 41, 112, 20),
    },
    "shrink-border-box": {
        "case": (0, 0, None, 79),
        "line": (0, 0, 200, 71),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 71, None, 8),
        "table": (40, 6, 120, 65),
        "cell0": (49, 16, 102, 20),
        "content0": (49, 16, 102, 20),
        "cell1": (49, 41, 102, 20),
        "content1": (49, 41, 102, 20),
    },
    "html-table-edges": {
        "case": (0, 0, None, 79),
        "line": (0, 0, 300, 71),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 71, None, 8),
        "table": (40, 6, 120, 65),
        "cell0": (49, 16, 102, 20),
        "content0": (49, 16, 102, 20),
        "cell1": (49, 41, 102, 20),
        "content1": (49, 41, 102, 20),
    },
    "cell-edges": {
        "case": (0, 0, None, 82),
        "line": (0, 0, 300, 74),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 74, None, 8),
        "table": (40, 10, 120, 64),
        "cell0": (40, 10, 120, 32),
        "content0": (46, 16, 108, 20),
        "cell1": (40, 42, 120, 32),
        "content1": (46, 48, 108, 20),
    },
    "empty": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 40, None, 8),
        "table": (40, 32, 120, 0),
    },
    "empty-cell": {
        "case": (0, 0, None, 70),
        "line": (0, 0, 300, 62),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 62, None, 8),
        "table": (40, 32, 120, 30),
        "cell0": (40, 32, 120, 30),
        "content0": (40, 32, 120, 0),
    },
    "no-baseline-cell": {
        "case": (0, 0, None, 60),
        "line": (0, 0, 300, 52),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 52, None, 8),
        "table": (40, 12, 120, 40),
        "cell0": (40, 12, 120, 20),
        "content0": (40, 12, 120, 20),
        "cell1": (40, 32, 120, 20),
        "content1": (40, 32, 120, 20),
    },
    "nowrap": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 150, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "wrap-before": {
        "case": (0, 0, None, 128),
        "line": (0, 0, 150, 120),
        "before": (0, 0, 40, 40),
        "after-peer": (0, 80, 40, 40),
        "after": (0, 120, None, 8),
        "table": (0, 40, 120, 40),
        "cell0": (0, 40, 120, 20),
        "content0": (0, 40, 120, 20),
        "cell1": (0, 60, 120, 20),
        "content1": (0, 60, 120, 20),
    },
    "wrap-after": {
        "case": (0, 0, None, 104),
        "line": (0, 0, 170, 96),
        "before": (0, 0, 40, 40),
        "after-peer": (0, 56, 40, 40),
        "after": (0, 96, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "intrinsic": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (88, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 48, 40),
        "cell0": (40, 16, 48, 20),
        "content0": (40, 16, 48, 20),
        "cell1": (40, 36, 48, 20),
        "content1": (40, 36, 48, 20),
    },
    "percent": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (190, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 150, 40),
        "cell0": (40, 16, 150, 20),
        "content0": (40, 16, 150, 20),
        "cell1": (40, 36, 150, 20),
        "content1": (40, 36, 150, 20),
    },
    "auto-margin": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "relative": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (44, 19, 120, 40),
        "cell0": (44, 19, 120, 20),
        "content0": (44, 19, 120, 20),
        "cell1": (44, 39, 120, 20),
        "content1": (44, 39, 120, 20),
    },
    "absolute": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, 40, 40),
        "after-peer": (40, 0, 40, 40),
        "after": (0, 40, None, 8),
        "table": (50, 10, 120, 40),
        "cell0": (50, 10, 120, 20),
        "content0": (50, 10, 120, 20),
        "cell1": (50, 30, 120, 20),
        "content1": (50, 30, 120, 20),
    },
    "float": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (120, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 40, None, 8),
        "table": (0, 0, 120, 40),
        "cell0": (0, 0, 120, 20),
        "content0": (0, 0, 120, 20),
        "cell1": (0, 20, 120, 20),
        "content1": (0, 20, 120, 20),
    },
    "flex": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "grid": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "block": {
        "case": (0, 0, None, 128),
        "line": (0, 0, 300, 120),
        "before": (0, 0, 40, 40),
        "after-peer": (0, 80, 40, 40),
        "after": (0, 120, None, 8),
        "table": (0, 40, 120, 40),
        "cell0": (0, 40, 120, 20),
        "content0": (0, 40, 120, 20),
        "cell1": (0, 60, 120, 20),
        "content1": (0, 60, 120, 20),
    },
    "clipped": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "border-collapse": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "html-table": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "nested": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "generated": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 40, None, 8),
    },
    "spaces": {
        "case": (0, 0, None, 58),
        "line": (0, 0, 300, 50),
        "before": (0, 0, 40, 40),
        "after-peer": (170, 0, 40, 40),
        "after": (0, 50, None, 8),
        "table": (45, 10, 120, 40),
        "cell0": (45, 10, 120, 20),
        "content0": (45, 10, 120, 20),
        "cell1": (45, 30, 120, 20),
        "content1": (45, 30, 120, 20),
    },
    "link": {
        "case": (0, 0, None, 64),
        "line": (0, 0, 300, 56),
        "before": (0, 0, 40, 40),
        "after-peer": (160, 0, 40, 40),
        "after": (0, 56, None, 8),
        "table": (40, 16, 120, 40),
        "cell0": (40, 16, 120, 20),
        "content0": (40, 16, 120, 20),
        "cell1": (40, 36, 120, 20),
        "content1": (40, 36, 120, 20),
    },
    "beside-float": {
        "case": (0, 0, None, 104),
        "line": (0, 0, 250, 96),
        "before": (80, 0, 40, 40),
        "after-peer": (0, 56, 40, 40),
        "after": (0, 96, None, 8),
        "table": (120, 16, 120, 40),
        "cell0": (120, 16, 120, 20),
        "content0": (120, 16, 120, 20),
        "cell1": (120, 36, 120, 20),
        "content1": (120, 36, 120, 20),
    },
    "plain-text": {
        "case": (0, 0, None, 48),
        "line": (0, 0, 300, 40),
        "before": (0, 0, None, 20),
        "after-peer": (132, 0, None, 20),
        "after": (0, 40, None, 8),
        "table": (12, 0, 120, 40),
        "cell0": (12, 0, 120, 20),
        "content0": (12, 0, 120, 20),
        "cell1": (12, 20, 120, 20),
        "content1": (12, 20, 120, 20),
    },
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/inline-table.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"] + [name + "-" + part for name, parts in CASES.items() for part in parts]
    with tempfile.TemporaryDirectory(prefix="wisp-inline-table-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x2900",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "500x2900", "--capture-resize", "700x2900",
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
            for name, parts in CASES.items():
                origin = boxes[name + "-case"]
                assert origin["x"] == 20 and origin["y"] == next_y, (index, name, origin, next_y)
                expected = CASES["block"] if name == "basic" and checked else parts
                for part, rect in expected.items():
                    box = boxes[name + "-" + part]
                    actual = (box["x"] - origin["x"], box["y"] - origin["y"], box["width"], box["height"])
                    for axis, (got, wanted) in enumerate(zip(actual, rect)):
                        if wanted is None:
                            if part in ("case", "after"):
                                wanted = (500 if index == 1 else 700) - 40
                            else:
                                continue
                        # libcss currently approximates 1ex as .6em. This font's
                        # Chromium x-height shifts middle alignment by 2px;
                        # table sizing and all other alignment cases are exact.
                        tolerance = 2 if name == "middle" and axis in (1, 3) else 0
                        assert abs(got - wanted) <= tolerance, (index, name, part, actual, rect)
                next_y += origin["height"] + 12
        # A display change rebuilds the appropriate box region; it must not
        # leave stale table/inline geometry behind when returning to baseline.
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"Inline tables: {len(CASES)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
