#!/usr/bin/env python3
"""Outside-marker first baselines, wrapped lines and nested blocks."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/outside-marker-baseline.html"
heights = {"tall": 42, "short": 36, "wrapped": 114, "padded": 58, "block-first": 52,
           "empty-before": 62, "floats": 42, "bfc": 48, "nested-list": 42, "relative": 42,
           "zero": 6, "image": 52, "block-floats": 42, "br-float": 90}
names = (*heights, "below-float")
labels = tuple(n + "-text" for n in names if n != "wrapped")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")
magick = shutil.which("magick")


def box(step, name):
    boxes = step[name]["boxes"]
    assert len(boxes) == 1, (name, step[name])
    return boxes[0]


with tempfile.TemporaryDirectory(prefix="wisp-outside-baseline-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x1200",
            "--capture-settle", "100", "--capture-timeout", "12000", "--enable_javascript=0"]
    for name in (*names, *labels, "direct-float", "floats-case", "block-floats-case",
                 "block-floats-float", "block-floats-clear", "br-floating", "br-next"):
        args += ["--capture-id", name]
    args += ["--capture-resize", "500x1200", "--capture-resize", "700x1200"]
    process = subprocess.run([*args, fixture.as_uri()], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = [{e["id"]: e for e in step["elements"]}
             for step in json.loads((destination / "steps.json").read_text())]
    for step in steps:
        for name, height in heights.items():
            b = box(step, name)
            assert b["border_box"]["height"] == height, (name, b["border_box"], height)
            assert b["list_marker"]["outside"], name
        for name in names:
            marker = box(step, name)["list_marker"]["border_box"]
            # Bundled font: 40px marker baseline=30px, 20px content baseline=24px.
            if name not in ("short", "wrapped", "below-float"):
                label = box(step, name + "-text")["border_box"]
                relative = 8 if name == "relative" else 0
                marker_ascent = 40 if name == "image" else 12 if name == "zero" else 30
                text_ascent = 6 if name == "zero" else 24
                assert marker["y"] + marker_ascent + relative == label["y"] + text_ascent, (name, marker, label)
        # Empty inline fragments consumed beside a blocking float are not the
        # first text line. The marker must follow the text below that float.
        owner = box(step, "below-float")["border_box"]
        marker = box(step, "below-float")["list_marker"]["border_box"]
        assert marker["y"] >= owner["y"] + 48, (marker, owner)
        assert marker["y"] + 30 == owner["y"] + owner["height"] - 12, (marker, owner)
        # Floats introduced on the pushed line move with it. Both the containing
        # flow-root and subsequent clearance must use their final geometry.
        direct = box(step, "direct-float")["border_box"]
        assert direct["y"] == box(step, "floats")["border_box"]["y"] + 6, direct
        assert box(step, "floats-case")["border_box"]["height"] == 54
        assert box(step, "block-floats-case")["border_box"]["height"] == 62
        parent = box(step, "block-floats")["border_box"]
        floating = box(step, "block-floats-float")["border_box"]
        cleared = box(step, "block-floats-clear")["border_box"]
        assert floating["y"] == parent["y"] + 6, (parent, floating)
        assert cleared["y"] == floating["y"] + floating["height"], (floating, cleared)
        br_float = box(step, "br-floating")["border_box"]
        following_line = box(step, "br-next")["border_box"]
        assert following_line["y"] == br_float["y"] + br_float["height"], (br_float, following_line)
    for name in names:
        for key in ("border_box", "list_marker"):
            assert box(steps[0], name)[key] == box(steps[-1], name)[key], (name, key)

    if magick:
        pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                check=True, capture_output=True).stdout
        assert len(pixels) == 700 * 1200 * 3

        def ink_bottom(rect, width=None):
            ys = [y for y in range(rect["y"], rect["y"] + rect["height"])
                  for x in range(rect["x"], rect["x"] + (rect["width"] if width is None else width))
                  if max(pixels[(y * 700 + x) * 3:(y * 700 + x) * 3 + 3]) < 100]
            assert ys, ("missing ink", rect)
            return max(ys) + 1

        for name in ("tall", "short", "padded", "block-first", "empty-before", "floats", "bfc", "nested-list",
                     "block-floats", "br-float", "image"):
            marker = box(steps[-1], name)["list_marker"]["border_box"]
            label = box(steps[-1], name + "-text")["border_box"]
            assert ink_bottom(marker) == ink_bottom(label, 12), (name, marker, label)
print("Outside markers: first baselines, wrapping, nested blocks, floats and resizing passed; "
      f"painted baselines {'passed' if magick else 'not checked (ImageMagick unavailable)'}.")
