#!/usr/bin/env python3
"""Marker text styling, box-property isolation, painting and restyling."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/marker-styling.html"
names = ("disclosure", "inside", "outside", "small", "ignored", "control", "decorated", "image", "none")
ids = ("details", "label", *names, *(name + "-label" for name in names[1:-1]))
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")
magick = shutil.which("magick")


def capture(directory, actions=()):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x600",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    process = subprocess.run([*args, fixture.as_uri()], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    return [{e["id"]: e for e in step["elements"]}
            for step in json.loads((directory / "steps.json").read_text())]


def box(step, name):
    boxes = step[name]["boxes"]
    assert len(boxes) == 1, (name, step[name])
    return boxes[0]


def style(b, property_name):
    match = re.search(r"(?:^| )" + re.escape(property_name) + r": ([^ ]+)", b["computed_style_dump"])
    assert match, (property_name, b)
    return match[1]


def verify(step):
    for name in names:
        owner = box(step, name)
        marker = owner["list_marker"]
        assert style(owner, "color") == "#ff123456", name
        assert style(owner, "font-size") == "16px", name
        assert style(marker, "position") == "static", name
        assert style(marker, "background-color") == "#00000000", name
        for edge in ("top", "right", "bottom", "left"):
            assert style(marker, "border-" + edge + "-style") == "none", name
            assert style(marker, "padding-" + edge) == "0px", name
            assert style(marker, "margin-" + edge) == "0px", name
    for name in ("disclosure", "inside", "outside"):
        marker = box(step, name)["list_marker"]
        assert style(marker, "color") == "#ffc02040", (name, marker)
        assert style(marker, "font-size") == "24px", name
        assert style(marker, "font-weight") == "bold", name
    assert style(box(step, "disclosure")["list_marker"], "font-style") == "italic"
    assert style(box(step, "small")["list_marker"], "font-size") == "8px"
    for name in ("small", "ignored"):
        assert style(box(step, name)["list_marker"], "color") == "#ff008040", name
    # Marker-specific list-style does not override the originating item's type
    # or placement, and forbidden dimensions must not disrupt the inline flow.
    ignored, control = (box(step, name)["list_marker"] for name in ("ignored", "control"))
    assert ignored["text"] == control["text"] == "1."
    assert not ignored["outside"] and not control["outside"]
    assert ignored["border_box"]["width"] == control["border_box"]["width"]
    assert box(step, "ignored-label")["border_box"]["x"] == box(step, "control-label")["border_box"]["x"]
    assert box(step, "inside")["list_marker"]["border_box"]["width"] > control["border_box"]["width"]
    image = box(step, "image")["list_marker"]["border_box"]
    assert (image["width"], image["height"]) == (12, 12), image
    assert box(step, "none")["list_marker"]["text"] == ""
    assert box(step, "none")["border_box"]["height"] == 0


with tempfile.TemporaryDirectory(prefix="wisp-marker-styles-") as tmp:
    root = Path(tmp)
    steps = capture(root / "resize", [("resize", "430x600"), ("resize", "700x600")])
    for step in steps:
        verify(step)
    for name in names:
        for key in ("border_box", "list_marker"):
            assert box(steps[0], name)[key] == box(steps[-1], name)[key], (name, key)

    if magick:
        for name, rgb in (("disclosure", (192, 32, 64)), ("inside", (192, 32, 64)),
                          ("outside", (192, 32, 64)), ("ignored", (0, 128, 64)), ("image", (0, 128, 0))):
            rect = box(steps[-1], name)["list_marker"]["border_box"]
            geometry = f'{rect["width"]}x{rect["height"]}+{rect["x"]}+{rect["y"]}'
            pixels = subprocess.run([magick, str(root / "resize/page.png"), "-crop", geometry,
                                     "-depth", "8", "RGB:-"], check=True, capture_output=True).stdout
            assert any(pixels[i:i + 3] == bytes(rgb) for i in range(0, len(pixels), 3)), (
                name, "marker colour was selected but not painted")

    steps = capture(root / "toggle", [("click-id", "disclosure"), ("click-id", "disclosure")])
    assert steps[1]["details"]["state"]["open"]
    opened = box(steps[1], "disclosure")["list_marker"]
    assert opened["text"] == "▾"
    assert style(opened, "color") == "#ff008040" and style(opened, "font-size") == "20px", opened
    assert not steps[2]["details"]["state"]["open"]
    verify(steps[2])
print("Marker styles: fonts, colours, box isolation, images, resizing and native restyling passed; "
      f"pixels {'passed' if magick else 'not checked (ImageMagick unavailable)'}.")
