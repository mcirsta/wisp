#!/usr/bin/env python3
"""Block-child marker fallback, shared margin scans and native restyle transitions."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Geometry is (item top/height, child top/height, following top/height), relative
# to the flow-root case. Auto sizes are Chromium references with the bundled font.
# Unlike Chromium's fallback, explicit height/max-height remain constraints.
CASES = {
    "empty": (13, 36, 13, 0, 66, 8),
    "short": (13, 36, 13, 20, 66, 8),
    "tall": (13, 60, 13, 60, 90, 8),
    "empty-margins": (17, 36, 17, 0, 70, 8),
    "short-margins": (13, 36, 13, 20, 66, 8),
    "large-margins": (90, 36, 90, 0, 216, 8),
    "negative-margins": (-16, 36, -16, 0, 8, 8),
    "negative-short": (-10, 36, -10, 20, 14, 8),
    "negative-item": (-13, 36, -13, 0, 6, 8),
    "fixed": (13, 10, 13, 0, 40, 8),
    "max": (13, 10, 13, 0, 40, 8),
    "min": (13, 60, 13, 0, 90, 8),
    "zero": (13, 0, 13, 0, 30, 8),
    "small-marker": (13, 36, 13, 0, 66, 8),
    "large-line": (13, 60, 13, 0, 90, 8),
    "padded": (13, 36, 21, 0, 66, 8),
    "bottom-pad": (17, 36, 17, 0, 70, 8),
    "top-pad": (13, 36, 29, 0, 66, 8),
    "flowroot": (13, 36, 24, 0, 66, 8),
    "flowroot-short": (13, 48, 24, 20, 78, 8),
    "nested": (13, 36, 13, 0, 66, 8),
    "nested-margin": (17, 36, 17, 0, 70, 8),
    "nested-float": (13, 36, 13, 0, 66, 8),
    "direct-float": (13, 36, 13, 0, 66, 8),
    "nested-float-root": (13, 48, 13, 0, 78, 8),
    "cleared": (13, 54, 61, 0, 84, 8),
    "none": (17, 0, 17, 0, 17, 8),
    "image": (13, 46, 13, 0, 76, 8),
    "block-bfc": (13, 36, 13, 0, 66, 8),
    "block-bfc-short": (13, 36, 13, 20, 66, 8),
    "padded-child": (13, 36, 13, 16, 66, 8),
    "relative-child": (13, 36, 28, 0, 66, 8),
    "text": (13, 42, 19, 36, 72, 8),
    "before-empty-text": (13, 42, 13, 0, 72, 8),
    "flex-empty": (13, 36, 13, 0, 66, 8),
    "grid-empty": (13, 36, 13, 0, 66, 8),
    "nested-list": (13, 36, 13, 0, 66, 8),
    "inside": (13, 42, 55, 0, 72, 8),
    "image-short": (13, 46, 13, 20, 76, 8),
    "image-fixed": (13, 10, 13, 0, 40, 8),
    "fixed-margins": (17, 10, 17, 0, 44, 8),
    "max-margins": (17, 10, 17, 0, 44, 8),
    "min-margins": (17, 60, 17, 0, 94, 8),
    "zero-margins": (17, 0, 17, 0, 34, 8),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/block-marker-fallback.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-block-markers-") as tmp:
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "700x1800",
                "--capture-timeout", "15000", "--capture-settle", "100", "--enable_javascript=0",
                "--capture-id", "switch"]
        for name in CASES:
            for suffix in ("case", "item", "child", "after"):
                args += ["--capture-id", name + "-" + suffix]
        args += ["--capture-resize", "500x1800", "--capture-resize", "700x1800",
                 "--capture-click-id", "switch", "--capture-click-id", "switch", fixture.as_uri()]
        process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=35)
        assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 5, len(steps)
        for index, report in enumerate(steps):
            elements = {e["id"]: e for e in report["elements"]}
            assert all(e["status"] == "ok" and e.get("boxes") for e in elements.values()), elements
            checked = index == 3
            assert elements["switch"]["state"]["checked"] == checked, (index, elements["switch"])
            for name, original in CASES.items():
                expected = list(original)
                if checked:
                    if name == "empty":
                        expected = [13, 42, 19, 36, 72, 8]
                    elif name == "none":
                        expected = [13, 36, 13, 0, 66, 8]
                    elif name in ("fixed", "max", "image-fixed", "fixed-margins", "max-margins"):
                        expected[1] = 46 if name == "image-fixed" else 36
                        expected[4] = expected[0] + expected[1] + 17
                case = elements[name + "-case"]["boxes"][0]["border_box"]
                for part, offset in (("item", 0), ("child", 2), ("after", 4)):
                    box = elements[name + "-" + part]["boxes"][0]["border_box"]
                    actual = [box["y"] - case["y"], box["height"]]
                    assert actual == expected[offset:offset + 2], (index, name, part, actual, expected)
                assert case["height"] == expected[4] + expected[5], (index, name, case, expected)
                # display:flow-root replaces list-item, so that control has no marker.
                if name not in ("inside", "flowroot-short") and (name != "none" or checked):
                    item = elements[name + "-item"]["boxes"][0]
                    assert "list_marker" in item, (index, name, item)
                    marker = item["list_marker"]["border_box"]
                    assert marker["y"] == item["border_box"]["y"], (index, name, item)
        # Native restyles may rebuild boxes; their allocation addresses are not
        # part of the layout result. Compare all remaining capture fields.
        for report in steps:
            for element in report["elements"]:
                for box in element["boxes"]:
                    box.pop("box_address", None)
        for step in (2, 4):
            for before, after in zip(steps[0]["elements"], steps[step]["elements"]):
                assert before == after, ("Round trip changed result", step, before["id"], before, after)
    print(f"Block-marker fallback: {len(CASES)} cases passed with resize and native restyle round trips.")


if __name__ == "__main__":
    main()
