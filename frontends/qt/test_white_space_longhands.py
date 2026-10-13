#!/usr/bin/env python3
"""CSS Text collapse/wrapping decisions and preserved breaks through real boxes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


CASES = ("collapse-nowrap", "preserve-wrap", "preserve-nowrap", "pre-line", "breaks-nowrap",
         "variable", "cross-node", "blank-lines", "empty", "inherit")
WIDTH, HEIGHT = 520, 1400


def main():
    fixture = Path(__file__).resolve().parents[2] / "test/white-space-longhands.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-white-space-") as tmp:
        destination = Path(tmp) / "capture"
        args = [str(Path(sys.argv[1]).resolve()), "--capture-dir", str(destination),
                "--capture-size", f"{WIDTH}x{HEIGHT}", "--capture-settle", "100", "--capture-timeout", "8000"]
        for name in CASES:
            for suffix in ("", "-ref"):
                args.extend(("--capture-id", name + suffix))
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        report = json.loads((destination / "inspection.json").read_text())
        boxes = {}
        for item in report["elements"]:
            assert item["status"] == "ok" and item.get("boxes"), item
            boxes[item["id"]] = item["boxes"][0]["content"]
        pixels = subprocess.run([sys.argv[2], str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                capture_output=True, check=True, timeout=10).stdout
        assert len(pixels) == WIDTH * HEIGHT * 3
        for row, name in enumerate(CASES):
            actual, reference = boxes[name], boxes[name + "-ref"]
            assert actual["height"] == reference["height"], (name, actual, reference)
            for y in range(row * 140, row * 140 + 120):
                offset = y * WIDTH * 3
                left = pixels[offset:offset + 240 * 3]
                right = pixels[offset + 260 * 3:offset + 500 * 3]
                assert left == right, f"{name}: pixel mismatch at y={y}"
        for name in ("pre-line", "breaks-nowrap", "cross-node"):
            assert boxes[name]["height"] == 48, (name, boxes[name])
        assert boxes["blank-lines"]["height"] == 72, boxes["blank-lines"]
        assert boxes["empty"]["height"] == 0, boxes["empty"]
    print(f"White-space longhands: {len(CASES)} geometry and pixel comparisons passed.")


if __name__ == "__main__":
    main()
