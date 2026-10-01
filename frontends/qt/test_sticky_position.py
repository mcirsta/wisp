#!/usr/bin/env python3
"""Sticky foundation: containing blocks, auto-inset flow and stacking at scroll zero.

Scroll-dependent painting is the next implementation step. Axis solver tests
already use separate Chromium measurements from this same fixture.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, magick = sys.argv[1:3]
fixture = Path(__file__).resolve().parents[2] / "test/sticky-position.html"
names = ("block", "flex", "inline-flex", "grid")
ids = [eid for name in names for eid in (f"anchor-{name}", f"{name}-direct", f"{name}-deep")]
ids += ["stack", "stack-header", "stack-child", "stack-cover"]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-sticky-") as tmp:
    for zoom in (1, 1.5):
        destination = Path(tmp) / f"zoom-{zoom}"
        args = [binary, "--capture-dir", str(destination), "--capture-size", "800x1200",
                "--capture-settle", "100", "--capture-timeout", "8000", f"--scale={int(zoom * 100)}"]
        for eid in ids:
            args.extend(["--capture-id", eid])
        result = subprocess.run([*args, fixture.as_uri()], env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        assert meta["zoom"] == zoom, meta
        report = json.loads((destination / "inspection.json").read_text())
        for element in report["elements"]:
            assert element["status"] == "ok" and element.get("boxes"), element
        boxes = {e["id"]: e["boxes"][0] for e in report["elements"]}
        for name in names:
            anchor = boxes[f"anchor-{name}"]["border_box"]
            for kind, dx in (("direct", 33), ("deep", 15)):
                box = boxes[f"{name}-{kind}"]
                cb = box.get("absolute_containing_block", {}).get("node", {}).get("id")
                assert cb == f"anchor-{name}", (name, kind, cb)
                rect = box["border_box"]
                actual = rect["x"] - anchor["x"], rect["y"] - anchor["y"], rect["width"], rect["height"]
                assert actual == (dx, 19, 11, 7), (name, kind, actual)
            assert (anchor["width"], anchor["height"]) == (192, 92), (name, anchor)

        pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                check=True, capture_output=True).stdout
        assert len(pixels) == 800 * 1200 * 3
        # A sticky auto-z context contains its high-z child below the z=1 sibling.
        # Also ensure the header background does not spread below its own box.
        stack = boxes["stack"]["border_box"]
        for dx, dy, rgb in ((20, 20, (34, 85, 204)), (70, 20, (34, 136, 68)), (70, 70, (255, 255, 255))):
            x, y = int((stack["x"] + dx) * zoom), int((stack["y"] + dy) * zoom)
            assert 0 <= x < 800 and 0 <= y < 1200, (x, y)
            offset = (y * 800 + x) * 3
            assert tuple(pixels[offset:offset + 3]) == rgb, (zoom, x, y, tuple(pixels[offset:offset + 3]), rgb)

print("Sticky foundation: direct/deep containing blocks, flow, stacking and background bounds passed.")
