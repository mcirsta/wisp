#!/usr/bin/env python3
"""Static layout geometry, checked against independent browser references.

The adjacent HTML/JSON pairs in test/ use a bundled font and fixed content sizes.
JSON rectangles are relative to each case, in x/y/width/height order. A pair of
rectangles means unchecked/checked; a single rectangle applies to both states.
One pixel of tolerance allows Wisp's integer layout to round fractional sizes.
Fixtures use a 20px body margin and a configurable case gap to check cumulative placement.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test" / (sys.argv[2] + ".html")
    expected = json.loads(fixture.with_suffix(".json").read_text())
    width, height = expected["viewport"]
    ids = ["switch"] + [name + "-" + part for name, boxes in expected["cases"].items() for part in boxes]
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-layout-geometry-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", f"360x{height}", "--capture-resize", f"{width}x{height}",
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
            for name, parts in expected["cases"].items():
                origin = boxes[name + "-case"]
                assert abs(origin["x"] - 20) <= 1 and abs(origin["y"] - next_y) <= 1, (
                    index, name, "case position", origin, next_y)
                for part, states in parts.items():
                    key = name + "-" + part
                    wanted = states[int(checked)] if isinstance(states[0], list) else states
                    if index == 1:
                        wanted = expected["narrow"].get(key, wanted)
                    box = boxes[key]
                    actual = (box["x"] - origin["x"], box["y"] - origin["y"], box["width"], box["height"])
                    assert all(abs(a - b) <= 1 for a, b in zip(actual, wanted)), (index, key, actual, wanted)
                    if part == "case":
                        next_y += wanted[3] + expected.get("case_gap", 50)
        # Returning to the initial viewport/style must restore all box geometry,
        # including baselines, flags and overflow, not just the asserted edges.
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"{fixture.stem}: {len(expected['cases'])} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
