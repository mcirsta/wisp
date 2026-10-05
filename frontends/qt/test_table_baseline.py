#!/usr/bin/env python3
"""Table first baselines, marker alignment and resize/native-restyle round trips."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# (item height, table top/height, marker top, content top/height pairs), case-relative.
# Chromium reference geometry; Wisp rounds the 17.5px middle float position down.
CASES = {
    "row": (42, (6, 36), 0, [(6, 36), (6, 36)]),
    "mixed": (40, (0, 40), 2, [(19, 20), (0, 40)]),
    "padding": (69, (0, 69), 6, [(20, 20), (4, 40)]),
    "edges": (68, (0, 68), 10, [(16, 36), (16, 36)]),
    "middle": (60, (0, 60), 12, [(20, 20), (0, 60)]),
    "bottom": (60, (0, 60), 12, [(40, 20), (0, 60)]),
    "no-baseline": (60, (0, 60), 30, [(0, 20), (0, 60)]),
    "all-top": (36, (0, 36), 6, [(0, 36), (0, 36)]),
    "all-bottom": (60, (0, 60), 30, [(40, 20), (0, 60)]),
    "all-middle": (60, (0, 60), 30, [(20, 20), (0, 60)]),
    "invalid-align": (40, (0, 40), 2, [(19, 20), (0, 40)]),
    "empty-first": (42, (6, 36), 0, [(30, 0), (6, 36)]),
    "empty-all": (30, (30, 0), 0, [(30, 0), (30, 0)]),
    "empty-sized": (54, (30, 24), 0, [(30, 0), (30, 0)]),
    "fixed-cell": (80, (0, 80), 2, [(19, 20), (0, 40)]),
    "row-height": (80, (0, 80), 2, [(19, 20), (0, 40)]),
    "nested-block": (44, (0, 44), 2, [(3, 41), (0, 40)]),
    "nested-flex": (44, (0, 44), 2, [(3, 41), (0, 40)]),
    "nested-grid": (44, (0, 44), 2, [(3, 41), (0, 40)]),
    "nested-table": (44, (0, 44), 2, [(8, 36), (0, 40)]),
    "clipped": (42, (6, 36), 0, [(30, 0), (6, 36)]),
    "float-only": (53, (0, 53), 15, [(0, 45), (13, 40)]),
    "relative": (44, (0, 44), 2, [(17, 36), (0, 40)]),
    "abs-first": (42, (6, 36), 0, [(6, 36), (6, 36)]),
    "two-rows": (76, (0, 76), 2, [(19, 20), (0, 40), (40, 36)]),
    "span": (76, (0, 76), 2, [(19, 20), (0, 40), (40, 36)]),
    "span-tall": (96, (0, 96), 23, [(0, 70), (40, 20), (60, 36)]),
    "span-top": (89, (0, 89), 23, [(0, 70), (0, 20), (53, 36)]),
    "collapsed": (44, (0, 44), 4, [(21, 20), (2, 40)]),
    "in-flex": (44, (8, 36), 2, [(8, 36), (8, 36)]),
    "in-grid": (44, (8, 36), 2, [(8, 36), (8, 36)]),
    "standalone": (40, (0, 40), None, [(19, 20), (0, 40)]),
    "float-shift": (80, (0, 80), 34, [(19, 45), (0, 80)]),
    "float-middle": (80, (0, 80), 34, [(17, 45), (0, 80)]),
    "float-bottom": (80, (0, 80), 34, [(35, 45), (0, 80)]),
    "nested-float": (80, (0, 80), 34, [(19, 0), (0, 80)]),
    "direct": (40, (0, 40), 2, []),
    "empty-padded": (49, (25, 24), 0, [(30, 0), (30, 0)]),
    "wrapped": (59, (0, 59), 2, [(19, 40), (0, 40)]),
    "groups": (76, (0, 76), 2, [(19, 20), (0, 40), (40, 36)]),
}

# Shared 2px collapsed borders reserve 1px on each side of the grid line.
EXTRA = {
    "collapsed-row0": ("collapsed", (1, 42)),
    "collapsed-cell0": ("collapsed", (1, 42)),
    "collapsed-cell1": ("collapsed", (1, 42)),
    "row-row0": ("row", (6, 36)),
    "row-cell0": ("row", (6, 36)),
    "row-cell1": ("row", (6, 36)),
    "mixed-row0": ("mixed", (0, 40)),
    "mixed-cell0": ("mixed", (0, 40)),
    "mixed-cell1": ("mixed", (0, 40)),
    "padding-row0": ("padding", (0, 69)),
    "padding-cell0": ("padding", (0, 69)),
    "padding-cell1": ("padding", (0, 69)),
    "edges-row0": ("edges", (11, 46)),
    "edges-cell0": ("edges", (11, 46)),
    "edges-cell1": ("edges", (11, 46)),
    "middle-row0": ("middle", (0, 60)),
    "middle-cell0": ("middle", (0, 60)),
    "middle-cell1": ("middle", (0, 60)),
    "bottom-row0": ("bottom", (0, 60)),
    "bottom-cell0": ("bottom", (0, 60)),
    "bottom-cell1": ("bottom", (0, 60)),
    "no-baseline-row0": ("no-baseline", (0, 60)),
    "no-baseline-cell0": ("no-baseline", (0, 60)),
    "no-baseline-cell1": ("no-baseline", (0, 60)),
    "all-top-row0": ("all-top", (0, 36)),
    "all-top-cell0": ("all-top", (0, 36)),
    "all-top-cell1": ("all-top", (0, 36)),
    "all-bottom-row0": ("all-bottom", (0, 60)),
    "all-bottom-cell0": ("all-bottom", (0, 60)),
    "all-bottom-cell1": ("all-bottom", (0, 60)),
    "all-middle-row0": ("all-middle", (0, 60)),
    "all-middle-cell0": ("all-middle", (0, 60)),
    "all-middle-cell1": ("all-middle", (0, 60)),
    "invalid-align-row0": ("invalid-align", (0, 40)),
    "invalid-align-cell0": ("invalid-align", (0, 40)),
    "invalid-align-cell1": ("invalid-align", (0, 40)),
    "empty-first-row0": ("empty-first", (6, 36)),
    "empty-first-cell0": ("empty-first", (6, 36)),
    "empty-first-cell1": ("empty-first", (6, 36)),
    "empty-all-row0": ("empty-all", (30, 0)),
    "empty-all-cell0": ("empty-all", (30, 0)),
    "empty-all-cell1": ("empty-all", (30, 0)),
    "empty-sized-row0": ("empty-sized", (30, 24)),
    "empty-sized-cell0": ("empty-sized", (30, 24)),
    "empty-sized-cell1": ("empty-sized", (30, 24)),
    "fixed-cell-row0": ("fixed-cell", (0, 80)),
    "fixed-cell-cell0": ("fixed-cell", (0, 80)),
    "fixed-cell-cell1": ("fixed-cell", (0, 80)),
    "row-height-row0": ("row-height", (0, 80)),
    "row-height-cell0": ("row-height", (0, 80)),
    "row-height-cell1": ("row-height", (0, 80)),
    "nested-block-row0": ("nested-block", (0, 44)),
    "nested-block-cell0": ("nested-block", (0, 44)),
    "nested-block-cell1": ("nested-block", (0, 44)),
    "nested-block-inner": ("nested-block", (3, 41)),
    "nested-flex-row0": ("nested-flex", (0, 44)),
    "nested-flex-cell0": ("nested-flex", (0, 44)),
    "nested-flex-cell1": ("nested-flex", (0, 44)),
    "nested-flex-inner": ("nested-flex", (3, 41)),
    "nested-grid-row0": ("nested-grid", (0, 44)),
    "nested-grid-cell0": ("nested-grid", (0, 44)),
    "nested-grid-cell1": ("nested-grid", (0, 44)),
    "nested-grid-inner": ("nested-grid", (3, 41)),
    "nested-table-row0": ("nested-table", (0, 44)),
    "nested-table-cell0": ("nested-table", (0, 44)),
    "nested-table-cell1": ("nested-table", (0, 44)),
    "nested-table-inner": ("nested-table", (8, 36)),
    "clipped-row0": ("clipped", (6, 36)),
    "clipped-cell0": ("clipped", (6, 36)),
    "clipped-cell1": ("clipped", (6, 36)),
    "float-only-row0": ("float-only", (0, 53)),
    "float-only-cell0": ("float-only", (0, 53)),
    "float-only-cell1": ("float-only", (0, 53)),
    "relative-row0": ("relative", (0, 44)),
    "relative-cell0": ("relative", (0, 44)),
    "relative-cell1": ("relative", (0, 44)),
    "abs-first-row0": ("abs-first", (6, 36)),
    "abs-first-cell0": ("abs-first", (6, 36)),
    "abs-first-cell1": ("abs-first", (6, 36)),
    "two-rows-row0": ("two-rows", (0, 40)),
    "two-rows-row1": ("two-rows", (40, 36)),
    "two-rows-cell0": ("two-rows", (0, 40)),
    "two-rows-cell1": ("two-rows", (0, 40)),
    "two-rows-cell2": ("two-rows", (40, 36)),
    "span-row0": ("span", (0, 40)),
    "span-row1": ("span", (40, 36)),
    "span-cell0": ("span", (0, 76)),
    "span-cell1": ("span", (0, 40)),
    "span-cell2": ("span", (40, 36)),
    "span-tall-row0": ("span-tall", (0, 60)),
    "span-tall-row1": ("span-tall", (60, 36)),
    "span-tall-cell0": ("span-tall", (0, 96)),
    "span-tall-cell1": ("span-tall", (0, 60)),
    "span-tall-cell2": ("span-tall", (60, 36)),
    "span-top-row0": ("span-top", (0, 53)),
    "span-top-row1": ("span-top", (53, 36)),
    "span-top-cell0": ("span-top", (0, 89)),
    "span-top-cell1": ("span-top", (0, 53)),
    "span-top-cell2": ("span-top", (53, 36)),
    "in-flex-row0": ("in-flex", (8, 36)),
    "in-flex-cell0": ("in-flex", (8, 36)),
    "in-flex-cell1": ("in-flex", (8, 36)),
    "in-flex-peer": ("in-flex", (0, 40)),
    "in-grid-row0": ("in-grid", (8, 36)),
    "in-grid-cell0": ("in-grid", (8, 36)),
    "in-grid-cell1": ("in-grid", (8, 36)),
    "in-grid-peer": ("in-grid", (0, 40)),
    "standalone-row0": ("standalone", (0, 40)),
    "standalone-cell0": ("standalone", (0, 40)),
    "standalone-cell1": ("standalone", (0, 40)),
    "float-shift-row0": ("float-shift", (0, 80)),
    "float-shift-cell0": ("float-shift", (0, 80)),
    "float-shift-cell1": ("float-shift", (0, 80)),
    "float-middle-row0": ("float-middle", (0, 80)),
    "float-middle-cell0": ("float-middle", (0, 80)),
    "float-middle-cell1": ("float-middle", (0, 80)),
    "float-bottom-row0": ("float-bottom", (0, 80)),
    "float-bottom-cell0": ("float-bottom", (0, 80)),
    "float-bottom-cell1": ("float-bottom", (0, 80)),
    "nested-float-row0": ("nested-float", (0, 80)),
    "nested-float-cell0": ("nested-float", (0, 80)),
    "nested-float-cell1": ("nested-float", (0, 80)),
    "nested-float-inner": ("nested-float", (19, 45)),
    "direct-row0": ("direct", (0, 40)),
    "direct-cell0": ("direct", (0, 40)),
    "direct-cell1": ("direct", (0, 40)),
    "empty-padded-row0": ("empty-padded", (25, 24)),
    "empty-padded-cell0": ("empty-padded", (25, 24)),
    "empty-padded-cell1": ("empty-padded", (25, 24)),
    "wrapped-row0": ("wrapped", (0, 59)),
    "wrapped-cell0": ("wrapped", (0, 59)),
    "wrapped-cell1": ("wrapped", (0, 59)),
    "groups-row0": ("groups", (0, 40)),
    "groups-row1": ("groups", (40, 36)),
    "groups-cell0": ("groups", (0, 40)),
    "groups-cell1": ("groups", (0, 40)),
    "groups-cell2": ("groups", (40, 36)),
}

def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/table-baseline.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    ids = ["switch"]
    for name, (_, _, _, children) in CASES.items():
        ids += [name + "-" + part for part in ("case", "item", "table", "after")]
        ids += [name + "-content" + str(i) for i in range(len(children))]
    ids += list(EXTRA)
    with tempfile.TemporaryDirectory(prefix="wisp-table-baselines-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x3400",
                "--capture-settle", "100", "--capture-timeout", "15000", "--enable_javascript=0"]
        for key in ids:
            args += ["--capture-id", key]
        args += ["--capture-resize", "500x3400", "--capture-resize", "700x3400",
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
            next_case_y = 20  # Body margin; each case has a 12px bottom margin.
            for name, values in CASES.items():
                height, table, marker_y, children = values
                if checked and name == "row":
                    height, table, children = 42, (0, 42), [(0, 36), (6, 36)]
                elif checked and name == "standalone":
                    children = [(10, 20), (0, 40)]
                origin = boxes[name + "-case"]["border_box"]["y"]
                assert origin == next_case_y, (index, name, "case top", origin, next_case_y)
                next_case_y += height + 8 + 12
                expected = {"case": (0, height + 8), "item": (0, height), "table": table, "after": (height, 8)}
                expected.update({"content" + str(i): rect for i, rect in enumerate(children)})
                for part, wanted in expected.items():
                    rect = boxes[name + "-" + part]["border_box"]
                    actual = (rect["y"] - origin, rect["height"])
                    assert actual == wanted, (index, name, part, actual, wanted)
                if marker_y is not None:
                    marker = boxes[name + "-item"]["list_marker"]["border_box"]
                    assert marker["y"] - origin == marker_y, (index, name, marker, marker_y)
            for key, (name, wanted) in EXTRA.items():
                origin = boxes[name + "-case"]["border_box"]["y"]
                if checked and name == "row":
                    wanted = (0, 42)
                rect = boxes[key]["border_box"]
                actual = (rect["y"] - origin, rect["height"])
                assert actual == wanted, (index, key, actual, wanted)
        # Allocation identity can change after a restyle; all reported layout
        # and style data must return to the initial result.
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for index in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[index]["elements"]):
                assert before == after, ("Round trip changed result", index, before["id"], before, after)
    print(f"Table baselines: {len(CASES)} cases passed with resizing and native restyles.")


if __name__ == "__main__":
    main()
