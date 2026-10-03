#!/usr/bin/env python3
"""Native disclosure markers and ordinary inside-list geometry/activation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/disclosure-markers.html"
ids = ("closed-details", "closed", "closed-text", "open-details", "open", "open-text", "wrap", "wrap-text",
       "none", "none-text", "block", "block-text", "outside", "outside-text", "custom", "custom-text",
       "generated", "generated-text", "primary", "secondary", "default", "empty", "empty-none",
       "block-first", "block-child", "rtl", "nine", "nine-text", "ten", "ten-text", "explicit",
       "reversed-first", "reversed-last", "narrow-item", "image-item", "image-text",
       "spaced-marker", "decorated", "decorated-text")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")


def capture(directory, actions=()):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x900",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    process = subprocess.run([*args, fixture.as_uri()], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((directory / "steps.json").read_text())
    return steps, [{e["id"]: e for e in step["elements"]} for step in steps]


def box(step, name):
    boxes = step[name]["boxes"]
    assert len(boxes) == 1, (name, boxes)
    return boxes[0]


def rect(step, name):
    return box(step, name)["border_box"]


def marker(step, name, expected, outside=False):
    m = box(step, name)["list_marker"]
    assert m["text"] == expected and m["outside"] == outside, (name, m)
    return m["border_box"]


with tempfile.TemporaryDirectory(prefix="wisp-markers-") as tmp:
    root = Path(tmp)
    _, steps = capture(root / "geometry", [("resize", "430x900"), ("resize", "700x900")])
    initial = steps[0]
    for step in steps:
        closed = marker(step, "closed", "▸")
        marker(step, "open", "▾")
        marker(step, "custom", "▪")
        marker(step, "primary", "▾")
        marker(step, "rtl", "◂")  # Glyph orientation; bidi line placement is separate.
        marker(step, "none", "", outside=True)
        marker(step, "empty-none", "")
        assert "list_marker" not in box(step, "block")
        assert "list_marker" not in box(step, "secondary")
        for name in ("closed", "open", "custom", "nine", "ten", "image-item"):
            label = "image-text" if name == "image-item" else name + "-text"
            m = box(step, name)["list_marker"]["border_box"]
            r, text = rect(step, name), rect(step, label)
            assert m["x"] == r["x"] < m["x"] + m["width"] < text["x"], (name, m, r, text)
            assert text["y"] == r["y"], (name, r, text)
        outside = marker(step, "outside", "▸", outside=True)
        assert outside["x"] + outside["width"] < rect(step, "outside-text")["x"]
        assert rect(step, "none")["x"] == rect(step, "none-text")["x"]
        assert rect(step, "wrap")["height"] == 72  # 3 lines at the fixture's 24px line height.
        assert rect(step, "empty")["height"] == 24 and rect(step, "empty-none")["height"] == 0
        assert rect(step, "block-child")["y"] == rect(step, "block-first")["y"] + 24
        assert rect(step, "generated-text")["x"] > rect(step, "closed-text")["x"]
        for name, text in (("nine", "9."), ("ten", "10."), ("explicit", "42."),
                           ("reversed-first", "2."), ("reversed-last", "1."), ("narrow-item", "XXIII.")):
            marker(step, name, text)
        assert box(step, "narrow-item")["list_marker"]["border_box"]["width"] > 8
        assert rect(step, "narrow-item")["height"] == 48  # Atomic marker, then unbreakable word.
        # The negative Korean counter contains an internal space. Wisp applies
        # the UA marker's nonwrapping default; the Chromium difference is tracked
        # in layout-correctness.md for the marker whitespace/styling follow-up.
        marker(step, "spaced-marker", "마이너스 일,")
        assert rect(step, "spaced-marker")["height"] == 48
        decorated = marker(step, "decorated", "▸")
        content = box(step, "decorated")["content"]
        assert (decorated["x"], decorated["y"]) == (content["x"], content["y"]), (decorated, content)
        assert decorated["height"] == 24 and decorated["width"] < 20, decorated
        image = box(step, "image-item")["list_marker"]
        assert image["box_type"] == "inline" and image["border_box"]["width"] == 12, image
    for name in ids:
        assert rect(initial, name) == rect(steps[-1], name), name

    closed = box(initial, "closed")["list_marker"]["border_box"]
    arrow = f'{closed["x"] + closed["width"] // 2},{closed["y"] + 12}'
    default = rect(initial, "default")
    default_arrow = f'{default["x"] + 4},{default["y"] + 12}'
    _, steps = capture(root / "activation", [("click-point", arrow), ("click-point", arrow),
        ("click-point", default_arrow)])
    assert steps[1]["closed-details"]["state"]["open"]
    marker(steps[1], "closed", "▾")
    assert not steps[2]["closed-details"]["state"]["open"]
    marker(steps[2], "closed", "▸")
    assert steps[3]["default"]["state"]["open"]

    # Selection traverses inside markers once, as ordinary children.
    first, last = rect(initial, "nine"), rect(initial, "explicit")
    drag = f'{first["x"]},{first["y"] + 12},{last["x"] + 180},{last["y"] + 12}'
    raw, _ = capture(root / "selection", [("drag", drag)])
    selected = raw[-1].get("selection", "")
    for text in ("9.", "10.", "42."):
        assert selected.count(text) == 1, selected
print("Disclosure markers: CSS overrides, wrapping, numbering, images, resize, clicks and selection passed.")
