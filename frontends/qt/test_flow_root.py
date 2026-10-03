#!/usr/bin/env python3
"""Flow-root contains floats and margins, preserves overflow, and can be restyled."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

# Border-box (x, y, width, height), relative to each case's content top-left.
# Fixed, font-independent dimensions independently checked against Chromium.
EXPECTED = {
    "overflow": ((0, 0, 100, 10), (0, 0, 140, 30), (0, 50, 300, 10)),
    "clipped": ((0, 0, 100, 10), (0, 0, 140, 30), (0, 50, 300, 10)),
    "contain": ((0, 0, 300, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "margins": ((0, 0, 300, 60), (0, 20, 300, 10), (0, 60, 300, 10)),
    "empty": ((0, 30, 300, 0), None, (0, 70, 300, 10)),
    "alongside": ((80, 0, 220, 10), (80, 0, 220, 10), (0, 10, 300, 10)),
    "below": ((0, 60, 260, 10), (0, 60, 260, 10), (0, 70, 300, 10)),
    "clear": ((0, 60, 300, 10), (0, 60, 300, 10), (0, 70, 300, 10)),
    "isolate": ((80, 0, 100, 30), (80, 20, 100, 10), (0, 30, 300, 10)),
    "inherit": ((0, 0, 300, 40), (0, 0, 300, 40), (0, 40, 300, 10)),
    "variable": ((0, 0, 300, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "span": ((0, 0, 300, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "positioned": ((10, 10, 120, 40), (10, 10, 40, 40), (0, 0, 300, 10)),
    "floating": ((0, 0, 100, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "flex": ((0, 0, 120, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "grid": ((0, 0, 120, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
    "pseudo": ((0, 0, 300, 70), (0, 40, 40, 30), (0, 70, 300, 10)),
    "switch": ((0, 0, 300, 40), (0, 0, 40, 40), (0, 40, 300, 10)),
}


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/flow-root.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    magick = shutil.which("magick")
    with tempfile.TemporaryDirectory(prefix="wisp-flow-root-") as tmp:
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x800",
                "--capture-settle", "75", "--capture-timeout", "12000",
                "--capture-resize", "500x900", "--capture-resize", "390x800",
                "--capture-click-label", "toggle", "--capture-click-label", "toggle",
                "--capture-scroll", "0,0", "--capture-id", "toggle"]
        for name, rectangles in EXPECTED.items():
            args += ["--capture-id", name + "-case"]
            for suffix, rectangle in zip(("root", "child", "after"), rectangles):
                if rectangle is not None:
                    args += ["--capture-id", name + "-" + suffix]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
        assert process.returncode == 0, process.stderr
        assert "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 6, len(steps)
        failures = []
        for index, report in enumerate(steps):
            assert all(e["status"] == "ok" and e.get("boxes") for e in report["elements"]), report["elements"]
            elements = {e["id"]: e for e in report["elements"]}
            boxes = {key: e["boxes"][0] for key, e in elements.items()}
            assert elements["toggle"]["state"]["checked"] == (index == 3), index
            for name, rectangles in EXPECTED.items():
                case = boxes[name + "-case"]["border_box"]
                for suffix, expected in zip(("root", "child", "after"), rectangles):
                    if expected is None:
                        continue
                    box = boxes[name + "-" + suffix]["border_box"]
                    if name == "switch" and index == 3:
                        expected = {"root": (0, 0, 300, 0), "child": (0, 0, 40, 40), "after": (0, 0, 300, 10)}[suffix]
                    actual = box["x"] - case["x"] - 1, box["y"] - case["y"] - 1, box["width"], box["height"]
                    if actual != expected:
                        failures.append(f"step {index}, {name}-{suffix}: expected {expected}, got {actual}")
                display = "block" if name == "switch" and index == 3 else "flow-root"
                assert f"display: {display} " in boxes[name + "-root"]["computed_style_dump"], (index, name, display)
        assert not failures, '\n'.join(failures)
        if magick:
            # Final action restores scroll/viewport: both overflow samples are visible.
            assert steps[-1]["scroll"] == {"x": 0, "y": 0}, steps[-1]["scroll"]
            pixels = subprocess.run([magick, str(dest / "page.png"), "-depth", "8", "RGB:-"],
                                    check=True, capture_output=True, timeout=5).stdout
            assert len(pixels) == 390 * 800 * 3
            for name, expected in (("overflow", (22, 136, 68)), ("clipped", (255, 255, 255))):
                case = boxes[name + "-case"]["border_box"]
                x, y = case["x"] + 121, case["y"] + 21
                offset = (y * 390 + x) * 3
                assert tuple(pixels[offset:offset + 3]) == expected, (name, x, y, pixels[offset:offset + 3])
    print(f"Flow-root: {len(EXPECTED)} cases passed before/after resize and native restyling; "
          f"overflow pixels {'passed' if magick else 'not checked (ImageMagick unavailable)' }.")


if __name__ == "__main__":
    main()
