#!/usr/bin/env python3
"""Fixed containing blocks, scroll coordinates, actual painting and hit testing."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, magick = sys.argv[1:3]
fixture = Path(__file__).resolve().parents[2] / "test/fixed-position.html"
ids = ["panel", "bottom", "percent", "static", "absolute", "nested", "nested-absolute",
       "nested-fixed", "stretch", "transform", "transform-fixed", "escape", "scroll-target",
       "padded-flex", "flex-left", "flex-right", "negative"]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-fixed-") as tmp:
    def capture(name, width, height, fragment="", point=None, zoom=1):
        destination = Path(tmp) / name
        args = [binary, "--capture-dir", str(destination), "--capture-size", f"{width}x{height}",
                "--capture-settle", "100", "--capture-timeout", "8000"]
        for eid in ids:
            args.extend(["--capture-id", eid])
        if point:
            args.extend(["--capture-point", f"{point[0]},{point[1]}"])
        if zoom != 1:
            args.append(f"--scale={int(zoom * 100)}")
        result = subprocess.run([*args, fixture.as_uri() + fragment], env=env,
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        assert meta["zoom"] == zoom, meta
        report = json.loads((destination / "inspection.json").read_text())
        for e in report["elements"]:
            assert e["status"] == "ok" and e.get("boxes"), e
        boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
        pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                check=True, capture_output=True).stdout
        assert len(pixels) == width * height * 3
        for x, y, rgb in [(5, height - 5, (22, 136, 68)),
                          (int(45 * zoom), height - int(85 * zoom), (34, 85, 204)),
                          (int(75 * zoom), height - int(85 * zoom), (204, 170, 17)),
                          (int(155 * zoom), height - int(65 * zoom), (204, 34, 85)),
                          (int(205 * zoom), height - int(65 * zoom), (0, 170, 170))]:
            offset = (y * width + x) * 3
            assert tuple(pixels[offset:offset + 3]) == rgb, (name, x, y, pixels[offset:offset + 3])
        return boxes, report, meta

    for width, height in [(640, 480), (800, 600)]:
        boxes, report, meta = capture(f"size-{width}", width, height, point=(205, height - 65))
        expected = {
            "bottom": (0, height - 40, width, 40),
            "percent": (width * 7 // 10, height // 10, width // 5, height // 5),
            "static": (33, 33, 20, 20),
            "absolute": (283, 203, 20, 20),
            "nested": (30, height - 100, 100, 50),
            "nested-absolute": (40, height - 90, 20, 20),
            "negative": (70, height - 90, 20, 20),
            "nested-fixed": (150, height - 70, 20, 20),
            "stretch": (10, 80, 5, height - 160),
            "transform-fixed": (113, 297, 20, 20),
            "escape": (200, height - 70, 20, 20),
            "padded-flex": (250, 10, 110, 50),
            "flex-left": (250, 50, 10, 10),
            "flex-right": (350, 50, 10, 10),
        }
        for eid, rect in expected.items():
            actual = tuple(boxes[eid][key] for key in ("x", "y", "width", "height"))
            assert actual == rect, (eid, actual, rect)
        assert any(h["node"]["id"] == "escape" for h in report["point"]["hits"]), report["point"]

    # A fragment navigation scrolls horizontally and vertically without JS.
    before, _, _ = capture("before-scroll", 640, 480)
    after, _, _ = capture("after-scroll", 640, 480, "#scroll-target")
    sx = after["bottom"]["x"] - before["bottom"]["x"]
    sy = after["bottom"]["y"] - before["bottom"]["y"]
    assert sx > 0 and sy > 500, (sx, sy)
    for eid in ("bottom", "percent", "static", "nested", "nested-absolute", "nested-fixed", "escape"):
        assert after[eid]["x"] - before[eid]["x"] == sx, (eid, before[eid], after[eid])
        assert after[eid]["y"] - before[eid]["y"] == sy, (eid, before[eid], after[eid])
    # A transformed ancestor contains its fixed child, so it scrolls with the document.
    assert after["transform-fixed"] == before["transform-fixed"]
    _, report, _ = capture("scrolled-hit", 640, 480, "#scroll-target", (sx + 205, sy + 415))
    assert any(h["node"]["id"] == "escape" for h in report["point"]["hits"]), report["point"]
    capture("zoomed-scroll", 640, 480, "#scroll-target", zoom=1.5)

print("Fixed positioning: viewport sizes, percentages, nesting, transforms, scroll, pixels and hits passed.")
