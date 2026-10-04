#!/usr/bin/env python3
"""Use the same computed inline edges for margin collapsing and line height."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


# name, edge kind, expression, outer/inner block CSS, inner widths at the
# two viewports, and whether the computed edge creates a line at each width.
PROFILES = (
    ("literal", "padding", "0px", "", "", (200, 400), (False, False)),
    ("percent-zero", "padding", "0%", "", "", (200, 400), (False, False)),
    # Firefox collapses all these provably zero edges. Chromium collapses
    # pure percentage cancellation but retains a line for mixed 0px + 0%.
    ("calc-percent-zero", "padding", "calc(0% + 0px)", "", "", (200, 400), (False, False)),
    ("cancel-percent", "padding", "calc(50% - 50%)", "", "", (200, 400), (False, False)),
    ("cancel-margin", "margin", "calc(50% - 50%)", "", "", (200, 400), (False, False)),
    ("cancel-mixed", "padding", "calc((50% + 5px) - (50% + 5px))", "", "",
     (200, 400), (False, False)),
    ("cancel-scaled", "padding", "calc(2 * (25% + 2px) - 50% - 4px)", "", "",
     (200, 400), (False, False)),
    ("cancel-divided", "padding", "calc((100% + 20px) / 2 - 50% - 10px)", "", "",
     (200, 400), (False, False)),
    ("cancel-fraction", "padding", "calc(50% - 50% + 0.5px)", "", "", (200, 400), (True, True)),
    ("fraction", "padding", "calc(0.75px - 0.25px)", "", "", (200, 400), (True, True)),
    ("text", "text", "0px", "", "", (200, 400), (True, True)),
    ("zero-padding", "padding", "calc(5px - 5px)", "", "", (200, 400), (False, False)),
    ("zero-margin", "margin", "calc(5px - 5px)", "", "", (200, 400), (False, False)),
    ("zero-border", "border", "calc(5px - 5px)", "", "", (200, 400), (False, False)),
    ("padding", "padding", "calc(10px - 5px)", "", "", (200, 400), (True, True)),
    ("margin", "margin", "calc(5px - 10px)", "", "", (200, 400), (True, True)),
    ("border", "border", "calc(10px - 5px)", "", "", (200, 400), (True, True)),
    ("em", "padding", "calc(1em - 20px)", "", "", (200, 400), (False, False)),
    ("percentage", "padding", "calc(50% - 100px)", "", "", (200, 400), (True, True)),
    ("negative", "margin", "calc(100px - 50%)", "", "", (200, 400), (True, True)),
    ("nested", "padding", "calc(50% - 50px)", "width:75%", "width:calc(100% - 50px)",
     (100, 250), (True, True)),
    ("minimum", "padding", "calc(50% - 100px)", "", "width:25%;min-width:200px",
     (200, 200), (True, True)),
    ("maximum", "padding", "calc(50% - 100px)", "", "width:100%;max-width:200px",
     (200, 200), (True, True)),
    ("border-box", "padding", "calc(50% - 75px)", "",
     "width:100%;box-sizing:border-box;padding:0 20px;border:solid;border-width:0 5px",
     (150, 350), (True, True)),
    ("auto-width", "padding", "calc(50% - 80px)", "",
     "margin:0 10px;padding:0 5px;border:solid;border-width:0 5px",
     (160, 360), (True, True)),
)


def cases():
    for profile in PROFILES:
        for side in ("left", "right"):
            for sign in (1, -1):
                yield f'{profile[0]}-{side}-{"positive" if sign == 1 else "negative"}', profile, side, sign


def fixture_html():
    font = (Path(__file__).resolve().parents[2] / "test/fonts/normal-400.ttf").as_uri()
    parts = [f'<!doctype html><meta charset="utf-8"><title>Calculated inline edges</title><style>'
             f'@font-face{{font-family:Test;src:url("{font}")}}'
             'body{margin:0;font:20px/20px Test}'
             '.case{display:flow-root;width:calc(100vw - 100px);border:1px solid;margin:10px}'
             '.before,.child,.after{height:30px;background:#ddd}</style>']
    for name, (_, kind, expression, outer, inner, _, _), side, sign in cases():
        edge = f'{"padding" if kind == "text" else kind}-{side}:{expression}'
        if kind == "border":
            edge += " solid"
        before_text = "H" if kind == "text" and side == "left" else ""
        after_text = "H" if kind == "text" and side == "right" else ""
        parts.append(
            f'<section class="case" id="{name}-case">'
            f'<div class="before" id="{name}-before" style="margin-bottom:{5 * sign}px"></div>'
            f'<div style="{outer}"><div id="{name}-inner" style="{inner}">'
            f'<span style="{edge}">{before_text}<div class="child" id="{name}-child" '
            f'style="margin:{13 * sign}px 0 {17 * sign}px"></div>{after_text}</span></div></div>'
            f'<div class="after" id="{name}-after" style="margin-top:{7 * sign}px"></div></section>')
    return "\n".join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-calc-inline-margins-") as tmp:
        fixture = Path(tmp) / "calc-inline-margins.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "300x800",
                "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
                "--capture-resize", "500x800", "--capture-resize", "300x800"]
        for name, *_ in cases():
            for suffix in ("case", "before", "inner", "child", "after"):
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
            for name, profile, side, sign in cases():
                case, before, child, after = (boxes[name + "-" + suffix]
                                              for suffix in ("case", "before", "child", "after"))
                has_edge = profile[-1][index % 2]
                # A computed nonzero edge reserves the explicit 20px line,
                # even when percentages resolve to zero or pixels round down.
                # Both browsers keep percentage-dependent edges; see the
                # profiles above for their mixed-zero cancellation difference.
                expected = (18 * sign + 20 if has_edge and side == "left" else 13 * sign,
                            24 * sign + 20 if has_edge and side == "right" else 17 * sign)
                gaps = (child["y"] - before["y"] - before["height"],
                        after["y"] - child["y"] - child["height"])
                assert gaps == expected, (index, name, gaps, expected)
                assert case["height"] == 92 + sum(expected), (index, name, case, expected)
                # The child has no horizontal edges, so its border width is
                # the containing block's content width (including box-sizing).
                assert child["width"] == profile[-2][index % 2], (index, name, child)
        assert steps[0]["elements"] == steps[-1]["elements"], "Resize round trip changed geometry"
    print(f"Calculated inline margins: {len(list(cases()))} cases passed before and after resizing.")


if __name__ == "__main__":
    main()
