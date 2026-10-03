#!/usr/bin/env python3
"""Clearance splits adjoining margins using the hypothetical border top."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def cases():
    # Expected (top, border height) pairs, relative to the case content top.
    # Authored integer geometry, checked against reference browsers and
    # CSS 2.1 sections 8.3.1/9.5.2.
    # Chromium adds an extra gap in some clear-without-clearance cases (the
    # 70/100px prefixes, exact float boundary and second clearing sibling).
    # Firefox preserves ordinary margin collapse, as required by the rule.
    for prefix, empty, after in ((10, 80, 115), (70, 90, 120), (100, 120, 150)):
        yield f"prefix-{prefix}", dict(prefix=prefix), dict(empty=(empty, 0), after=(after, 10))
    for prefix, empty, parent, height, after in ((10, 80, 30, 60, 140), (70, 95, 95, 0, 120),
                                                (100, 125, 125, 0, 150)):
        yield f"wrapped-{prefix}", dict(prefix=prefix, wrapped=True), dict(
            empty=(empty, 0), parent=(parent, height), after=(after, 10))
    yield "clear-left", dict(clear="left"), dict(empty=(80, 0), after=(115, 10))
    yield "wrong-side", dict(clear="right"), dict(empty=(30, 0), after=(60, 10))
    yield "right-float", dict(side="right", clear="right"), dict(empty=(80, 0), after=(115, 10))
    yield "right-wrong-side", dict(side="right", clear="left"), dict(empty=(30, 0), after=(60, 10))
    yield "clear-none", dict(clear="none"), dict(empty=(30, 0), after=(60, 10))
    yield "nonempty", dict(height=10), dict(empty=(80, 10), after=(140, 10))
    yield "bfc-cleared", dict(height=10, style="overflow:hidden"), dict(empty=(80, 10), after=(140, 10))
    yield "bfc-beside-float", dict(height=10, clear="none", style="overflow:hidden", width=290), dict(
        empty=(30, 10), after=(90, 10))
    yield "nonempty-wrapped", dict(height=10, wrapped=True), dict(
        empty=(80, 10), parent=(30, 60), after=(140, 10))
    yield "padded", dict(style="padding:5px 0 7px"), dict(empty=(80, 12), after=(142, 10))
    yield "bordered", dict(style="border-top:3px solid"), dict(empty=(80, 3), after=(133, 10))
    yield "parent-bottom-padding", dict(height=10, wrapped=True, parent_style="padding-bottom:3px"), dict(
        empty=(80, 10), parent=(30, 88), after=(168, 10))
    yield "inner-margins", dict(inner="margin:30px 0 40px"), dict(empty=(80, 0), inner=(80, 0), after=(90, 10))
    yield "inner-prevents-clear", dict(inner="margin:100px 0 40px"), dict(
        empty=(110, 0), inner=(110, 0), after=(110, 10))
    yield "negative-top", dict(top=-15), dict(empty=(80, 0), after=(130, 10))
    yield "negative-before", dict(before=-20), dict(empty=(80, 0), after=(115, 10))
    yield "large-before", dict(before=100), dict(empty=(110, 0), after=(110, 10))
    yield "negative-after", dict(after=-50), dict(empty=(80, 0), after=(40, 10))
    for clear in (False, True):
        yield f"empty-sibling-{int(clear)}", dict(between="margin:5px 0 35px;" + ("clear:both" if clear else "")), dict(
            empty=(80, 0), between=(90, 0), after=(115, 10))
    yield "parent-with-later-content", dict(wrapped=True, inside_after=True), dict(
        empty=(80, 0), parent=(30, 95), after=(115, 10))
    yield "at-float-bottom", dict(prefix=60), dict(empty=(80, 0), after=(110, 10))
    # Clearance can exist with a numeric value of zero or less: it changes
    # which margins adjoin even when no positive offset must be inserted.
    yield "zero-clearance", dict(top=50), dict(empty=(80, 0), after=(80, 10))
    yield "zero-clearance-wrapped", dict(top=50, wrapped=True), dict(
        empty=(80, 0), parent=(30, 50), after=(130, 10))
    yield "negative-clearance", dict(top=60), dict(empty=(80, 0), after=(80, 10))
    yield "float-between", dict(before=40, float_after=True, float_height=20, top=30), dict(
        empty=(70, 0), after=(90, 10))
    yield "float-between-nonempty", dict(before=40, float_after=True, float_height=20, top=30, height=10), dict(
        empty=(70, 10), after=(130, 10))
    yield "minimum-height", dict(style="min-height:60px"), dict(empty=(80, 60), after=(190, 10))
    # Percentage height becomes definite during layout; it must then separate
    # the box's margins rather than be classified as a self-collapsing empty.
    for name, parent_style in (("percentage-fixed", "height:200px"),
                               ("percentage-ratio", "aspect-ratio:3/2")):
        yield name, dict(wrapped=True, style="height:50%", parent_style=parent_style), dict(
            empty=(80, 100), parent=(30, 200), after=(280, 10))
    # Indefinite percentages retain Chromium's auto-height collapse behavior;
    # Firefox instead separates the margins, leaving this parent 10px shorter.
    yield "percentage-auto", dict(wrapped=True, style="height:50%"), dict(
        empty=(80, 0), parent=(30, 60), after=(140, 10))


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><title>Float clearance</title><style>',
             'html,body{margin:0}.case{border:1px solid;width:300px;margin-bottom:10px;overflow:hidden}</style>']
    # Border prevents parent-top collapse; overflow contains the test float.
    for name, c, _ in cases():
        inner = f'<div id="{name}-inner" style="{c["inner"]}"></div>' if "inner" in c else ""
        empty = (f'<div id="{name}-empty" style="clear:{c.get("clear", "both")};'
                 f'margin:{c.get("top", 15)}px 0 25px;height:{c.get("height", 0)}px;'
                 f'{c.get("style", "")}">{inner}</div>')
        between = f'<div id="{name}-between" style="{c["between"]}"></div>' if "between" in c else ""
        after = f'<div id="{name}-after" style="height:10px;margin-top:{c.get("after", 50)}px"></div>'
        if c.get("wrapped"):
            empty = (f'<div id="{name}-parent" style="{c.get("parent_style", "")}">'
                     f'{empty}{between}{after if c.get("inside_after") else ""}</div>')
            between = ""
            if c.get("inside_after"):
                after = ""
        floating = (f'<div id="{name}-float" style="float:{c.get("side", "left")};'
                    f'width:10px;height:{c.get("float_height", 80)}px"></div>')
        preceding = f'<div style="height:{c.get("prefix", 10)}px;margin-bottom:{c.get("before", 20)}px"></div>'
        start = preceding + floating if c.get("float_after") else floating + preceding
        parts.append(f'<div class="case" id="{name}-case">{start}{empty}{between}{after}</div>')
    return '\n'.join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-float-clearance-") as tmp:
        fixture = Path(tmp) / "clearance.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x800",
                "--capture-settle", "100", "--capture-timeout", "8000",
                "--capture-resize", "500x900", "--capture-resize", "390x800"]
        for name, _, expected in cases():
            for suffix in ("case", "float", *expected):
                args.extend(("--capture-id", name + "-" + suffix))
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
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
            for name, c, expected in cases():
                case = boxes[name + "-case"]
                float_geometry = (50, 20) if c.get("float_after") else (0, 80)
                for suffix, geometry in {"float": float_geometry, **expected}.items():
                    box = boxes[name + "-" + suffix]
                    actual = box["y"] - case["y"] - 1, box["height"]
                    if actual != geometry:
                        failures.append(f"step {index}, {name}-{suffix}: expected {geometry}, got {actual}")
                    width = 10 if suffix == "float" else c.get("width", 300) if suffix == "empty" else 300
                    if box["width"] != width:
                        failures.append(f"step {index}, {name}-{suffix}: expected width {width}, got {box['width']}")
                # The containing BFC includes whichever ends lower: the float
                # or the final in-flow block, plus its two 1px borders.
                height = max(sum(float_geometry), sum(expected["after"])) + 2
                if case["height"] != height:
                    failures.append(f"step {index}, {name}: expected height {height}, got {case['height']}")
        assert not failures, '\n'.join(failures)
    print(f"Float clearance: {len(list(cases()))} cases passed before and after resizing.")


if __name__ == "__main__":
    main()
