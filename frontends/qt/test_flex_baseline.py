#!/usr/bin/env python3
"""Flex first baselines, marker alignment and resize/native-restyle round trips."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# (item height, flex top/height, marker top, child top/height pairs), relative to each case.
CASES = {
    "flex-hidden": (30, (10, 20), 0, [(10, 20), (10, 20)]),
    "flex-hidden-tall": (86, (6, 80), 0, [(6, 80), (6, 80)]),
    "hidden-tall": (86, (6, 80), 0, [(6, 80), (6, 80)]),
    "column-baseline": (78, (6, 72), 0, [(6, 36), (42, 36)]),
    "nested-auto": (100, (0, 100), 48, [(10, 80), (32, 36)]),
    "nested-stretch": (100, (0, 100), 38, [(0, 100), (0, 100)]),
    "nested-baseline": (44, (0, 44), 2, [(3, 41), (0, 40)]),
    "reverse-column-edges": (100, (2, 98), 0, [(55, 36), (9, 36)]),
    "reverse-column-sized": (132, (0, 132), 34, [(89, 36), (43, 36)]),
    "reverse-row-edges": (56, (2, 54), 0, [(9, 36), (9, 36)]),
    "row": (42, (6, 36), 0, [(6, 36), (6, 36)]),
    "reverse": (45, (9, 36), 0, [(9, 36), (9, 36)]),
    "order": (45, (9, 36), 0, [(9, 36), (9, 36)]),
    "column": (78, (6, 72), 0, [(6, 36), (42, 36)]),
    "column-reverse": (81, (9, 72), 0, [(45, 36), (9, 36)]),
    "baseline": (40, (0, 40), 2, [(19, 20), (0, 40)]),
    "baseline-margins": (69, (0, 69), 6, [(20, 20), (4, 40)]),
    "baseline-second": (42, (6, 36), 0, [(6, 36), (6, 36)]),
    "center": (80, (0, 80), 16, [(22, 36), (10, 60)]),
    "end": (80, (0, 80), 38, [(44, 36), (20, 60)]),
    "empty-first": (48, (12, 36), 0, [(12, 18), (12, 36)]),
    "empty-all": (36, (12, 24), 0, [(12, 18), (12, 24)]),
    "wrap": (78, (6, 72), 0, [(6, 36), (42, 36)]),
    "wrap-reverse": (81, (9, 72), 0, [(45, 36), (9, 36)]),
    "wrap-baseline": (76, (0, 76), 2, [(19, 20), (0, 40), (40, 36)]),
    "reverse-baseline": (100, (0, 100), 62, [(79, 20), (60, 40)]),
    "auto-margin": (80, (0, 80), 2, [(44, 36), (0, 40)]),
    "padded": (60, (0, 60), 6, [(7, 46), (7, 46)]),
    "clipped": (46, (10, 36), 0, [(10, 20), (10, 36)]),
    "nested": (42, (1, 41), 0, [(1, 41), (1, 41)]),
    "relative": (42, (6, 36), 0, [(15, 36), (6, 36)]),
    "abs-first": (42, (6, 36), 0, [(76, 36), (6, 36)]),
    "no-items": (36, (0, 0), 0, []),
    "fixed-child": (42, (6, 36), 0, [(6, 18), (6, 36)]),
    "baseline-negative": (36, (0, 36), 2, [(16, 20), (0, 40)]),
    "wrapped-text": (78, (6, 72), 0, [(6, 72), (6, 72)]),
    "float-child": (42, (6, 36), 0, [(6, 36), (6, 36)]),
    "standalone": (40, (0, 40), None, [(19, 20), (0, 40)]),
    "inline": (36, (0, 36), None, [(0, 36)]),
    "inline-wrap": (72, (0, 72), None, [(0, 72)]),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/flex-baseline.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"]
    for name, (_, _, _, children) in CASES.items():
        ids += [name + "-" + part for part in ("case", "item", "flex", "after")]
        ids += [name + "-child" + str(i) for i in range(len(children))]
    with tempfile.TemporaryDirectory(prefix="wisp-flex-baselines-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x2400",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "500x2400", "--capture-resize", "700x2400",
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
            for name, values in CASES.items():
                height, flex, marker_y, children = values
                if checked and name == "row":
                    height, flex, children = 36, (0, 36), [(0, 36), (0, 36)]
                elif checked and name == "standalone":
                    children = [(10, 20), (0, 40)]
                origin = boxes[name + "-case"]["border_box"]["y"]
                expected = {"item": (0, height), "flex": flex, "after": (height, 8)}
                expected.update({"child" + str(i): rect for i, rect in enumerate(children)})
                for part, wanted in expected.items():
                    rect = boxes[name + "-" + part]["border_box"]
                    actual = (rect["y"] - origin, rect["height"])
                    assert actual == wanted, (index, name, part, actual, wanted)
                if marker_y is not None:
                    marker = boxes[name + "-item"]["list_marker"]["border_box"]
                    assert marker["y"] - origin == marker_y, (index, name, marker, marker_y)
        # Allocation identity can change after a restyle; all reported layout
        # and style data must return to the initial result.
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"Flex baselines: {len(CASES)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
