#!/usr/bin/env python3
"""Collapsed spaces retain their source font through wrapping and restyles."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Chromium at 96 DPI, using the bundled normal-400 font. Each state contains
# (case height, relative line border box, relative following-element border box).
# States are unchecked/checked; the checkbox changes preceding and source fonts.
CASES = {
    "block": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "flex": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "grid": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "table": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "image": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "text-end": (
        (66, (0, 0, 69, 66), (29, 32, 40, 10)),
        (60, (0, 0, 57, 60), (17, 26, 40, 10))),
    "leading-text": (
        (60, (0, 0, 97, 60), (57, 26, 40, 10)),
        (60, (0, 0, 97, 60), (57, 26, 40, 10))),
    "inside": (
        (66, (0, 0, 114, 66), (74, 32, 40, 10)),
        (60, (0, 0, 97, 60), (57, 26, 40, 10))),
    "wrapper": (
        (72, (0, 0, 95, 72), (55, 38, 40, 10)),
        (72, (0, 0, 95, 72), (55, 38, 40, 10))),
    "first-space": (
        (66, (0, 0, 74, 66), (34, 32, 40, 10)),
        (60, (0, 0, 57, 60), (17, 26, 40, 10))),
    "empty-first": (
        (72, (0, 0, 85, 72), (45, 38, 40, 10)),
        (72, (0, 0, 85, 72), (45, 38, 40, 10))),
    "leading": (
        (66, (0, 0, 40, 66), (0, 32, 40, 10)),
        (60, (0, 0, 40, 60), (0, 26, 40, 10))),
    "trailing": (
        (72, (0, 0, 69, 72), (29, 38, 40, 10)),
        (72, (0, 0, 57, 72), (17, 38, 40, 10))),
    "wrap-fit": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (60, (0, 0, 85, 60), (45, 26, 40, 10))),
    "wrap-narrow": (
        (120, (0, 0, 84, 120), (0, 86, 40, 10)),
        (120, (0, 0, 84, 120), (0, 86, 40, 10))),
    "nowrap-atom": (
        (120, (0, 0, 84, 120), (0, 86, 40, 10)),
        (120, (0, 0, 84, 120), (0, 86, 40, 10))),
    "nowrap-parent": (
        (60, (0, 0, 84, 60), (45, 26, 40, 10)),
        (60, (0, 0, 84, 60), (45, 26, 40, 10))),
    "internal-break": (
        (132, (0, 0, 100, 132), (53, 98, 40, 10)),
        (60, (0, 0, 100, 60), (58, 26, 40, 10))),
    "internal-fit": (
        (192, (0, 0, 100, 192), (0, 158, 40, 10)),
        (120, (0, 0, 100, 120), (0, 86, 40, 10))),
    "reflow": (
        (66, (0, 0, 160, 66), (111, 32, 40, 10)),
        (60, (0, 0, 160, 60), (58, 26, 40, 10))),
    "restyle": (
        (60, (0, 0, 85, 60), (45, 26, 40, 10)),
        (72, (0, 0, 95, 72), (55, 38, 40, 10))),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/space-font.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"] + [name + "-" + part for name in CASES for part in ("case", "line", "peer")]
    with tempfile.TemporaryDirectory(prefix="wisp-space-font-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "400x1900",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "340x1900", "--capture-resize", "400x1900",
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
                height, line, peer = states[int(checked)]
                if name == "reflow" and index == 1:
                    # The viewport-dependent width becomes 100px: identical to
                    # the fixed-width internal-break case. Widening rejoins it.
                    height, line, peer = CASES["internal-break"][0]
                origin = boxes[name + "-case"]
                assert (origin["x"], origin["y"], origin["height"]) == (20, next_y, height), (
                    index, name, origin, next_y, height)
                next_y += height + 10
                for part, wanted in (("line", line), ("peer", peer)):
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
    print(f"Space fonts: {len(CASES)} cases passed with wrapping, resizing and native restyles.")


if __name__ == "__main__":
    main()
