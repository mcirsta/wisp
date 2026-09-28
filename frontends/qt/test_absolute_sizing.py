#!/usr/bin/env python3
"""Exercise every auto/set combination in the absolute axis equations."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Each axis has three independent auto/set inputs: start, size, and end.
# The intrinsic content is 80x60; the containing block is 400x300.
HORIZONTAL = [(0, 80), (10, 80), (0, 80), (10, 80),
              (300, 80), (10, 370), (300, 80), (10, 80)]
VERTICAL = [(0, 60), (10, 60), (0, 70), (10, 70),
            (220, 60), (10, 270), (210, 70), (10, 70)]


def cases():
    for mask in range(8):
        style = []
        for bit, declarations in [(1, "left:10px;top:10px"),
                                  (2, "width:80px;height:70px"),
                                  (4, "right:20px;bottom:20px")]:
            if mask & bit:
                style.append(declarations)
        x, width = HORIZONTAL[mask]
        y, height = VERTICAL[mask]
        yield f"auto-{mask}", ";".join(style), (x, y, width, height)
    base = "left:10px;top:10px;right:20px;bottom:20px;width:80px;height:70px;"
    yield "both-margins", base + "margin:auto", (155, 110, 80, 70)
    yield "start-margins", base + "margin-left:auto;margin-top:auto", (300, 210, 80, 70)
    yield "end-margins", base + "margin-right:auto;margin-bottom:auto", (10, 10, 80, 70)
    yield "negative-margins", base + "width:500px;height:400px;margin:auto", (10, -55, 500, 400)
    yield "edges", "left:10px;top:10px;width:80px;height:70px;padding:5px;border:2px solid;margin:3px", (13, 13, 94, 84)
    yield "minimum", "right:20px;top:10px;width:40px;min-width:100px;height:70px", (280, 10, 100, 70)
    yield "maximum", "left:10px;top:10px;width:500px;max-width:120px;height:70px", (10, 10, 120, 70)
    yield "flex-content", base + "display:flex", (10, 10, 80, 70)
    yield "grid-content", base + "display:grid", (10, 10, 80, 70)


def fixture_html():
    parts = ["""<!doctype html><meta charset="utf-8"><style>
        html,body{margin:0}.parent{position:relative;width:400px;height:300px;margin-bottom:20px}
        .absolute{position:absolute;background:#369}.inside{width:80px;height:60px}
        </style>"""]
    for name, style, _ in cases():
        parts.append(f'<div class="parent" id="{name}-parent"><div class="absolute" id="{name}" '
                     f'style="{style}"><div class="inside"></div></div></div>')
    return "\n".join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-absolute-sizing-") as tmp:
        fixture = Path(tmp) / "absolute-sizing.html"
        fixture.write_text(fixture_html())
        destination = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "800x600",
                "--capture-settle", "100", "--capture-timeout", "8000"]
        for name, _, _ in cases():
            args.extend(("--capture-id", name, "--capture-id", name + "-parent"))
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        report = json.loads((destination / "inspection.json").read_text())
        for item in report["elements"]:
            assert item["boxes"], f"Missing layout box for {item['id']}"
        boxes = {item["id"]: item["boxes"][0] for item in report["elements"]}
        failures = []
        for name, _, expected in cases():
            box = boxes[name]["border_box"]
            parent = boxes[name + "-parent"]["content"]
            actual = box["x"] - parent["x"], box["y"] - parent["y"], box["width"], box["height"]
            if actual != expected:
                failures.append(f"{name}: expected {expected}, got {actual}")
        assert not failures, "\n".join(failures)
    print(f"Absolute sizing: {len(list(cases()))} geometry checks passed.")


if __name__ == "__main__":
    main()
