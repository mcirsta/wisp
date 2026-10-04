#!/usr/bin/env python3
"""Logical start/end, flex-relative alignment, wrapping and native restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Chromium border boxes relative to container content: (x, y, width, height).
EXPECTED = {
    "grid-start": ("start", [(0, 0, 40, 20), (50, 0, 60, 40)]),
    "grid-end": ("end", [(0, 80, 40, 20), (50, 60, 60, 40)]),
    "grid-flex-start": ("flex-start", [(0, 0, 40, 20), (50, 0, 60, 40)]),
    "grid-flex-end": ("flex-end", [(0, 80, 40, 20), (50, 60, 60, 40)]),
    "grid-center": ("center", [(0, 40, 40, 20), (50, 30, 60, 40)]),
    "grid-stretch": ("stretch", [(0, 0, 40, 20), (50, 0, 60, 40)]),
    "row-nowrap-start": ("start", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "row-nowrap-end": ("end", [(0, 80, 40, 20), (40, 60, 60, 40)]),
    "row-nowrap-flex-start": ("flex-start", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "row-nowrap-flex-end": ("flex-end", [(0, 80, 40, 20), (40, 60, 60, 40)]),
    "row-nowrap-center": ("center", [(0, 40, 40, 20), (40, 30, 60, 40)]),
    "row-nowrap-stretch": ("stretch", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "row-wrap-reverse-start": ("start", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "row-wrap-reverse-end": ("end", [(0, 80, 40, 20), (40, 60, 60, 40)]),
    "row-wrap-reverse-flex-start": ("flex-start", [(0, 80, 40, 20), (40, 60, 60, 40)]),
    "row-wrap-reverse-flex-end": ("flex-end", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "row-wrap-reverse-center": ("center", [(0, 40, 40, 20), (40, 30, 60, 40)]),
    "row-wrap-reverse-stretch": ("stretch", [(0, 80, 40, 20), (40, 60, 60, 40)]),
    "row-reverse-nowrap-start": ("start", [(90, 0, 40, 20), (30, 0, 60, 40)]),
    "row-reverse-nowrap-end": ("end", [(90, 80, 40, 20), (30, 60, 60, 40)]),
    "row-reverse-nowrap-flex-start": ("flex-start", [(90, 0, 40, 20), (30, 0, 60, 40)]),
    "row-reverse-nowrap-flex-end": ("flex-end", [(90, 80, 40, 20), (30, 60, 60, 40)]),
    "row-reverse-nowrap-center": ("center", [(90, 40, 40, 20), (30, 30, 60, 40)]),
    "row-reverse-nowrap-stretch": ("stretch", [(90, 0, 40, 20), (30, 0, 60, 40)]),
    "row-reverse-wrap-reverse-start": ("start", [(90, 0, 40, 20), (30, 0, 60, 40)]),
    "row-reverse-wrap-reverse-end": ("end", [(90, 80, 40, 20), (30, 60, 60, 40)]),
    "row-reverse-wrap-reverse-flex-start": ("flex-start", [(90, 80, 40, 20), (30, 60, 60, 40)]),
    "row-reverse-wrap-reverse-flex-end": ("flex-end", [(90, 0, 40, 20), (30, 0, 60, 40)]),
    "row-reverse-wrap-reverse-center": ("center", [(90, 40, 40, 20), (30, 30, 60, 40)]),
    "row-reverse-wrap-reverse-stretch": ("stretch", [(90, 80, 40, 20), (30, 60, 60, 40)]),
    "column-nowrap-start": ("start", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "column-nowrap-end": ("end", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "column-nowrap-flex-start": ("flex-start", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "column-nowrap-flex-end": ("flex-end", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "column-nowrap-center": ("center", [(45, 0, 40, 20), (35, 20, 60, 40)]),
    "column-nowrap-stretch": ("stretch", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "column-wrap-reverse-start": ("start", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "column-wrap-reverse-end": ("end", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "column-wrap-reverse-flex-start": ("flex-start", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "column-wrap-reverse-flex-end": ("flex-end", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "column-wrap-reverse-center": ("center", [(45, 0, 40, 20), (35, 20, 60, 40)]),
    "column-wrap-reverse-stretch": ("stretch", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "column-reverse-nowrap-start": ("start", [(0, 80, 40, 20), (0, 40, 60, 40)]),
    "column-reverse-nowrap-end": ("end", [(90, 80, 40, 20), (70, 40, 60, 40)]),
    "column-reverse-nowrap-flex-start": ("flex-start", [(0, 80, 40, 20), (0, 40, 60, 40)]),
    "column-reverse-nowrap-flex-end": ("flex-end", [(90, 80, 40, 20), (70, 40, 60, 40)]),
    "column-reverse-nowrap-center": ("center", [(45, 80, 40, 20), (35, 40, 60, 40)]),
    "column-reverse-nowrap-stretch": ("stretch", [(0, 80, 40, 20), (0, 40, 60, 40)]),
    "column-reverse-wrap-reverse-start": ("start", [(0, 80, 40, 20), (0, 40, 60, 40)]),
    "column-reverse-wrap-reverse-end": ("end", [(90, 80, 40, 20), (70, 40, 60, 40)]),
    "column-reverse-wrap-reverse-flex-start": ("flex-start", [(90, 80, 40, 20), (70, 40, 60, 40)]),
    "column-reverse-wrap-reverse-flex-end": ("flex-end", [(0, 80, 40, 20), (0, 40, 60, 40)]),
    "column-reverse-wrap-reverse-center": ("center", [(45, 80, 40, 20), (35, 40, 60, 40)]),
    "column-reverse-wrap-reverse-stretch": ("stretch", [(90, 80, 40, 20), (70, 40, 60, 40)]),
    "rtl-nowrap-start": ("start", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "rtl-nowrap-end": ("end", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "rtl-nowrap-flex-start": ("flex-start", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "rtl-nowrap-flex-end": ("flex-end", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "rtl-nowrap-center": ("center", [(45, 0, 40, 20), (35, 20, 60, 40)]),
    "rtl-nowrap-stretch": ("stretch", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "rtl-wrap-reverse-start": ("start", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "rtl-wrap-reverse-end": ("end", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "rtl-wrap-reverse-flex-start": ("flex-start", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "rtl-wrap-reverse-flex-end": ("flex-end", [(90, 0, 40, 20), (70, 20, 60, 40)]),
    "rtl-wrap-reverse-center": ("center", [(45, 0, 40, 20), (35, 20, 60, 40)]),
    "rtl-wrap-reverse-stretch": ("stretch", [(0, 0, 40, 20), (0, 20, 60, 40)]),
    "multi-row-wrap-start": ("start", [(0, 0, 40, 20), (40, 0, 60, 40), (0, 40, 70, 70)]),
    "multi-row-wrap-end": ("end", [(0, 20, 40, 20), (40, 0, 60, 40), (0, 40, 70, 70)]),
    "multi-row-wrap-flex-start": ("flex-start", [(0, 0, 40, 20), (40, 0, 60, 40), (0, 40, 70, 70)]),
    "multi-row-wrap-flex-end": ("flex-end", [(0, 20, 40, 20), (40, 0, 60, 40), (0, 40, 70, 70)]),
    "multi-row-wrap-reverse-start": ("start", [(0, 60, 40, 20), (40, 60, 60, 40), (0, -10, 70, 70)]),
    "multi-row-wrap-reverse-end": ("end", [(0, 80, 40, 20), (40, 60, 60, 40), (0, -10, 70, 70)]),
    "multi-row-wrap-reverse-flex-start": ("flex-start", [(0, 80, 40, 20), (40, 60, 60, 40), (0, -10, 70, 70)]),
    "multi-row-wrap-reverse-flex-end": ("flex-end", [(0, 60, 40, 20), (40, 60, 60, 40), (0, -10, 70, 70)]),
    "multi-column-wrap-start": ("start", [(0, 0, 40, 20), (0, 20, 60, 40), (60, 0, 70, 70)]),
    "multi-column-wrap-end": ("end", [(20, 0, 40, 20), (0, 20, 60, 40), (60, 0, 70, 70)]),
    "multi-column-wrap-flex-start": ("flex-start", [(0, 0, 40, 20), (0, 20, 60, 40), (60, 0, 70, 70)]),
    "multi-column-wrap-flex-end": ("flex-end", [(20, 0, 40, 20), (0, 20, 60, 40), (60, 0, 70, 70)]),
    "multi-column-wrap-reverse-start": ("start", [(70, 0, 40, 20), (70, 20, 60, 40), (0, 0, 70, 70)]),
    "multi-column-wrap-reverse-end": ("end", [(90, 0, 40, 20), (70, 20, 60, 40), (0, 0, 70, 70)]),
    "multi-column-wrap-reverse-flex-start": ("flex-start", [(90, 0, 40, 20), (70, 20, 60, 40), (0, 0, 70, 70)]),
    "multi-column-wrap-reverse-flex-end": ("flex-end", [(70, 0, 40, 20), (70, 20, 60, 40), (0, 0, 70, 70)]),
    "absolute-start": ("start", [(0, 0, 40, 20)]),
    "absolute-end": ("end", [(0, 80, 40, 20)]),
    "absolute-flex-start": ("flex-start", [(0, 80, 40, 20)]),
    "absolute-flex-end": ("flex-end", [(0, 0, 40, 20)]),
    "self-override": ("start", [(0, 80, 40, 20), (40, 0, 60, 40)]),
    "auto-margin": ("end", [(0, 0, 40, 20), (40, 60, 60, 40)]),
    "both-auto": ("end", [(0, 40, 40, 20), (40, 60, 60, 40)]),
    "edges": ("end", [(6, 69, 46, 26), (56, 60, 60, 40)]),
    "overflow": ("end", [(0, -40, 40, 140), (40, 60, 60, 40)]),
    "natural-start": ("start", [(0, 0, 40, 20), (40, 0, 60, 40)]),
    "natural-end": ("end", [(0, 80, 40, 20), (50, 60, 60, 40)]),
    "reverse-stretch": ("stretch", [(0, 70, 40, 30), (40, 60, 60, 40)]),
}

def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/logical-alignment.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"]
    for name, (_, rects) in EXPECTED.items():
        ids += [name + "-container"]
        ids += [name + "-" + part for part in ("item", "peer", "third")[:len(rects)]]
    with tempfile.TemporaryDirectory(prefix="wisp-logical-alignment-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x1000",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "600x1000", "--capture-resize", "800x1000",
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
            boxes = {key: e["boxes"][0] for key, e in elements.items()}
            for name, (keyword, rects) in EXPECTED.items():
                if checked:
                    if name in ("row-nowrap-start", "grid-start"):
                        rects = EXPECTED[name.replace("-start", "-end")][1]
                        keyword = "end"
                    elif name == "self-override":
                        rects = EXPECTED["row-nowrap-start"][1]
                    elif name == "column-nowrap-end":
                        rects = EXPECTED["rtl-nowrap-end"][1]
                    elif name == "multi-row-wrap-end":
                        rects = EXPECTED["multi-row-wrap-reverse-end"][1]
                parent = boxes[name + "-container"]
                assert parent["layout_style"]["align-items"] == keyword, (index, name, parent["layout_style"])
                origin = parent["content"]
                for part, wanted in zip(("item", "peer", "third"), rects):
                    rect = boxes[name + "-" + part]["border_box"]
                    actual = (rect["x"] - origin["x"], rect["y"] - origin["y"], rect["width"], rect["height"])
                    assert actual == wanted, (index, name, part, actual, wanted)
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            assert steps[0]["elements"] == steps[index]["elements"], ("Round trip changed result", index)
    print(f"Logical alignment: {len(EXPECTED)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
