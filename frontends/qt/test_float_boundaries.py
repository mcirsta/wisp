#!/usr/bin/env python3
"""Text and tables use the first available float bottom, without a pixel gap."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


# Floats are (side, width, height, top margin, bottom margin).
# Expected coordinates are relative to the case content edge. The bundled
# font advances 12px per H; all line and cell heights are explicitly 20px.
PROFILES = (
    ("left", (("left", 180, 40, 0, 0),), 48, 0, 40),
    ("right", (("right", 180, 40, 0, 0),), 48, 0, 40),
    ("short-left", (("left", 90, 40, 0, 0), ("right", 90, 80, 0, 0)), 48, 0, 40),
    ("short-right", (("left", 90, 80, 0, 0), ("right", 90, 40, 0, 0)), 48, 90, 40),
    ("both", (("left", 90, 40, 0, 0), ("right", 90, 80, 0, 0)), 144, 0, 80),
    ("equal", (("left", 90, 40, 0, 0), ("right", 90, 40, 0, 0)), 48, 0, 40),
    ("stacked", (("left", 180, 40, 0, 0), ("left", 180, 20, 0, 0)), 48, 0, 60),
    ("margin", (("left", 180, 40, 5, 7),), 48, 0, 52),
    ("exact", (("left", 152, 40, 0, 0),), 48, 152, 0),
    ("oversize", (("left", 180, 40, 0, 0),), 240, 0, 40),
    ("zero", (("left", 180, 0, 0, 0),), 48, 0, 0),
)


def cases():
    for kind in ("text", "wrapped", "nested", "table"):
        for name, floats, width, x, y in PROFILES:
            yield f"{kind}-{name}", kind, floats, width, x, y


def fixture_html():
    font = (Path(__file__).resolve().parents[2] / "test/fonts/normal-400.ttf").as_uri()
    parts = [f'<!doctype html><meta charset="utf-8"><title>Float bottom boundaries</title><style>'
             f'@font-face{{font-family:Test;src:url("{font}")}}'
             'body{margin:0;font:20px/20px Test}.case{display:flow-root;width:200px;border:1px solid;margin:10px}'
             'table{border-spacing:0;table-layout:fixed}td{padding:0}</style>']
    for name, kind, floats, width, _, _ in cases():
        content = ""
        for i, (side, w, h, top, bottom) in enumerate(floats):
            content += (f'<div id="{name}-float-{i}" style="float:{side};width:{w}px;height:{h}px;'
                        f'margin:{top}px 0 {bottom}px;background:#ddd"></div>')
        if kind == "table":
            content += (f'<table id="{name}-target" style="width:{width}px"><tr><td>'
                        '<div style="height:20px;background:#666"></div></td></tr></table>')
        elif kind in ("wrapped", "nested"):
            word = "H" * (width // 12)
            if kind == "nested":
                word = f"<span>{word}</span>"
            content += f'<span id="{name}-target">{word}</span>'
        else:
            content += "H" * (width // 12)
        # The next in-flow edge reveals line height even beside a taller float.
        content += f'<div id="{name}-after" style="height:8px;background:#6a6"></div>'
        parts.append(f'<section class="case" id="{name}-case">{content}</section>')
    return "\n".join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-float-boundaries-") as tmp:
        fixture = Path(tmp) / "float-boundaries.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x800",
                "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
                "--capture-resize", "500x900", "--capture-resize", "390x800"]
        for name, kind, floats, _, _, _ in cases():
            suffixes = ["case", "after", *(f"float-{i}" for i in range(len(floats)))]
            if kind == "table":
                suffixes.append("target")
            for suffix in suffixes:
                args += ["--capture-id", name + "-" + suffix]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
        assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 3, len(steps)
        for index, step in enumerate(steps):
            assert all(e["status"] == "ok" and e.get("boxes") for e in step["elements"]), step["elements"]
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in step["elements"]}
            for name, kind, floats, width, x, y in cases():
                case, after = boxes[name + "-case"], boxes[name + "-after"]
                assert after["y"] - case["y"] - 1 == y + 20, (index, name, after, case, y)
                bottom = after["y"] + after["height"]
                for i, (_, _, _, _, margin_bottom) in enumerate(floats):
                    floating = boxes[f"{name}-float-{i}"]
                    bottom = max(bottom, floating["y"] + floating["height"] + margin_bottom)
                assert case["y"] + case["height"] == bottom + 1, (index, name, case, bottom)
                if kind == "table":
                    target = boxes[name + "-target"]
                    geometry = target["x"] - case["x"] - 1, target["y"] - case["y"] - 1, target["width"]
                    assert geometry == (x, y, width), (index, name, geometry, (x, y, width))
        assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed geometry"

        # Inline element inspection describes the zero-width opening fragment.
        # Check the actual text boxes in the final dump, including wrapped spans.
        expected = {name + "-case": (x, y, width, 20)
                    for name, kind, _, width, x, y in cases() if kind != "table"}
        found, owner = {}, None
        for line in (dest / "boxes.txt").read_text().splitlines():
            match = re.search(r"\bID:(\S+)", line)
            if match and match[1].endswith("-case"):
                owner = match[1]
            if owner in expected and " TEXT " in line:
                match = re.search(r"\bx(-?\d+) y(-?\d+) w(\d+) h(\d+)", line)
                assert match and owner not in found, (owner, line)
                found[owner] = tuple(map(int, match.groups()))
        assert found == expected, (found, expected)
    print(f"Float boundaries: {len(list(cases()))} text/table cases passed before and after resizing.")


if __name__ == "__main__":
    main()
