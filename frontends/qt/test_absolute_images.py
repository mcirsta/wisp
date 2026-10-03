#!/usr/bin/env python3
"""Position loaded images using their replaced size, before solving inset equations."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

IMAGE = Path(__file__).resolve().parents[2] / "test/test_data/object_fit/test_small.png"  # 100 x 75


def cases():
    # Every auto/set combination of the two insets and the specified size.
    for ratio in (False, True):
        for mask in range(8):
            style = "aspect-ratio:2;" if ratio else ""
            for bit, declarations in ((1, "left:10px;top:10px;"),
                                      (2, "width:80px;height:40px;"),
                                      (4, "right:20px;bottom:20px;")):
                if mask & bit:
                    style += declarations
            width, height = (80, 40) if mask & 2 else (100, 50 if ratio else 75)
            x = 10 if mask & 1 else 400 - 20 - width if mask & 4 else 0
            y = 10 if mask & 1 else 300 - 20 - height if mask & 4 else 0
            yield f'{"ratio" if ratio else "natural"}-{mask}', style, (x, y, width, height), ""
    base = "left:10px;right:20px;top:10px;bottom:20px;width:120px;"
    yield "natural-width", base, (10, 10, 120, 90), ""
    yield "ratio-width", base + "aspect-ratio:2", (10, 10, 120, 60), ""
    yield "auto-ratio", base + "aspect-ratio:auto 2", (10, 10, 120, 90), ""
    yield "explicit-height", base + "height:40px", (10, 10, 120, 40), ""
    yield "auto-margins", base + "margin:auto", (135, 100, 120, 90), ""
    yield "start-margin", base + "margin-top:auto", (10, 190, 120, 90), ""
    yield "end-margin", base + "margin-bottom:auto", (10, 10, 120, 90), ""
    yield "minimum", base + "min-height:120px;margin:auto", (135, 85, 120, 120), ""
    yield "maximum", base + "max-height:40px;margin:auto", (135, 125, 120, 40), ""
    yield "conflict", base + "min-height:120px;max-height:40px;margin:auto", (135, 85, 120, 120), ""
    yield "fixed-minimum", base + "height:40px;min-height:120px;margin:auto", (135, 85, 120, 120), ""
    yield "bottom-maximum", "right:20px;bottom:20px;width:120px;max-height:40px", (260, 240, 120, 40), ""
    yield "intrinsic-maximum", "inset:0;max-height:30px;margin:auto", (180, 135, 40, 30), ""
    yield "intrinsic-minimum", "inset:0;min-height:120px;margin:auto", (120, 90, 160, 120), ""
    yield "height-minimum", "inset:0;height:60px;min-height:120px;margin:auto", (120, 90, 160, 120), ""
    yield "width-maximum", "inset:0;max-width:80px;margin:auto", (160, 120, 80, 60), ""
    yield "padded-percent", "inset:0;width:120px;max-height:25%;margin:auto", (150, 120, 120, 80), "padding:10px"
    yield "natural-edges", base + "padding:5px;border:1px solid;box-sizing:border-box", (10, 10, 120, 93), ""
    yield "ratio-edges", base + "aspect-ratio:2;padding:5px;border:1px solid;box-sizing:border-box", (10, 10, 120, 60), ""
    yield "fixed-position", base + "position:fixed", (10, 10, 120, 90), "transform:translate(0,0)"
    yield "svg-natural", base, (10, 10, 120, 60), ""
    yield "svg-ratio", base + "aspect-ratio:1", (10, 10, 120, 120), ""
    yield "flow-minimum", "position:static;display:block;width:120px;min-height:120px", (0, 0, 120, 120), ""
    yield "flow-maximum", "position:static;display:block;width:120px;max-height:40px", (0, 0, 120, 40), ""


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><title>Positioned image sizing</title><style>',
             'html,body{margin:0}.parent{position:relative;width:400px;height:300px;margin-bottom:20px}',
             'img{position:absolute;background:#9dc}</style>']
    for name, style, _, parent_style in cases():
        image = IMAGE.parents[2] / "aspect-ratio-image.svg" if name.startswith("svg-") else IMAGE
        parts.append(f'<div class="parent" id="{name}-parent" style="{parent_style}">'
                     f'<img id="{name}" src="{image.as_uri()}" style="{style}"></div>')
    return '\n'.join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-absolute-images-") as tmp:
        fixture = Path(tmp) / "absolute-images.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "800x600",
                "--capture-settle", "100", "--capture-timeout", "8000",
                "--capture-resize", "900x700", "--capture-resize", "800x600"]
        for name, _, _, _ in cases():
            args.extend(("--capture-id", name, "--capture-id", name + "-parent"))
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        assert "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 3, len(steps)
        failures = []
        for index, report in enumerate(steps):
            assert all(e["status"] == "ok" and e.get("boxes") for e in report["elements"]), report["elements"]
            boxes = {e["id"]: e["boxes"][0] for e in report["elements"]}
            for name, _, expected, _ in cases():
                box, parent = boxes[name]["border_box"], boxes[name + "-parent"]["padding_box"]
                actual = box["x"] - parent["x"], box["y"] - parent["y"], box["width"], box["height"]
                if actual != expected:
                    failures.append(f"step {index}, {name}: expected {expected}, got {actual}")
        assert not failures, '\n'.join(failures)
    print(f"Positioned images: {len(list(cases()))} sizing/inset cases passed before and after resizing.")


if __name__ == "__main__":
    main()
