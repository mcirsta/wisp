#!/usr/bin/env python3
"""Float-only blocks collapse margins without borrowing later siblings' offsets."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def cases():
    # Geometry is (top, border height), relative to the case's content edge.
    # Authored integer sizes, checked against Chromium and CSS 2.2 section 8.3.1.
    for name, options, top, after in (
        ("plain", {}, 13, 17),
        ("listed", dict(tag="li"), 17, 17),
        ("wrapped", dict(wrap=True), 17, 17),
        ("nested", dict(nested=True), 13, 17),
        ("prefix", dict(prefix=10, previous=20), 30, 30),
        ("bottom-larger", dict(bottom=50), 13, 50),
        ("following-larger", dict(after=70), 13, 70),
        ("mixed", dict(bottom=-17), 13, -4),
        ("clear", dict(clear="both"), 13, 61),
        ("clear-large", dict(clear="both", after=70), 13, 70),
        ("nested-margins", dict(nested=True, inner="margin:23px 0 29px"), 29, 29),
        ("nested-after", dict(nested=True, inner="margin:23px 0 29px", after=70), 29, 70),
        ("following-negative", dict(after=-30), 13, -13),
        ("right-float", dict(side="right"), 13, 17),
        ("negative-prefix", dict(prefix=40, top=-13, bottom=-17), 27, 23),
    ):
        expected = dict(empty=(top, 0), floating=(top, 48), after=(after, 8))
        if options.get("wrap"):
            expected["wrapper"] = (top, 0)
        if options.get("nested"):
            expected["inner"] = (top, 0)
        yield name, options, expected
    for name, style, height, float_top, after in (
        ("padded", "padding:5px 0 7px", 12, 18, 42),
        ("minimum", "min-height:20px", 20, 13, 50),
        ("flowroot", "display:flow-root", 48, 13, 78),
    ):
        yield name, dict(style=style), dict(empty=(13, height), floating=(float_top, 48), after=(after, 8))
    yield "no-float", dict(no_float=True), dict(empty=(13, 0), after=(17, 8))
    yield "last", dict(last=True), dict(empty=(13, 0), floating=(13, 48))
    for wrapped in (False, True):
        expected = dict(empty=(13, 56), floating=(13, 48), cleared=(61, 0), after=(86, 8))
        if wrapped:
            expected["wrapper"] = (13, 56)
        yield f"inner-clear-{int(wrapped)}", dict(inner_clear=True, wrap=wrapped), expected
        top = 17 if wrapped else 13
        expected = dict(empty=(top, 0), floating=(top, 48), second=(top + 48, 20), after=(17, 8))
        if wrapped:
            expected["wrapper"] = (top, 0)
        yield f"two-floats-{int(wrapped)}", dict(two=True, wrap=wrapped), expected

    for name, options, expected in (
        ("negative", {}, dict(empty=(-13, 0), floating=(-13, 48), after=(-17, 8))),
        ("negative-right", dict(side="right"), dict(empty=(-13, 0), floating=(-13, 48), after=(-17, 8))),
        ("negative-deep", dict(top=-73), dict(empty=(-73, 0), floating=(-73, 48), after=(-73, 8))),
        ("negative-clear", dict(top=-73, clear="both"),
         dict(empty=(-73, 0), floating=(-73, 48), after=(-25, 8))),
        ("negative-unmatched-block-clear", dict(top=-73, clear="right"),
         dict(empty=(-73, 0), floating=(-73, 48), after=(-73, 8))),
        ("negative-br-clear", dict(top=-73, break_clear="left", style="font:20px/20px sans-serif"),
         dict(empty=(-73, 48), floating=(-73, 48), after=(-42, 8))),
        ("negative-br-unmatched", dict(top=-73, break_clear="right", style="font:20px/20px sans-serif"),
         dict(empty=(-73, 20), floating=(-73, 48), after=(-70, 8))),
        ("negative-two", dict(top=-73, two=True),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-25, 20), after=(-73, 8))),
        ("negative-clear-float", dict(top=-73, two=True, second_width=40, second_clear="left"),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-25, 20), after=(-73, 8))),
        ("negative-unmatched-clear", dict(top=-73, two=True, second_width=40, second_clear="right"),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-73, 20), after=(-73, 8))),
        ("negative-after-float", dict(top=-100, prefix=60, preceding=True),
         dict(prefix=(0, 60), preceding=(60, 16), empty=(-40, 0), floating=(60, 48), after=(-40, 8))),
    ):
        yield name, dict(top=-13, bottom=-17) | options, expected

    # Floats before/between block children must not commit the parent's top
    # before those children's adjoining margins have been collected.
    for name, options, top, child_top, child_height, parent_height, float_top, after in (
        ("before-child", {}, 29, 29, 0, 0, 29, 29),
        ("between-children", dict(float_after_child=True), 29, 29, 0, 0, 29, 29),
        ("before-wrapped", dict(wrap=True), 29, 29, 0, 0, 29, 29),
        ("before-right", dict(side="right"), 29, 29, 0, 0, 29, 29),
        ("before-following", dict(after=70), 29, 29, 0, 0, 29, 70),
        ("before-last", dict(last=True), 29, 29, 0, 0, 29, None),
        ("before-prefix", dict(prefix=60, previous=31), 91, 91, 0, 0, 91, 91),
        ("before-negative", dict(child="margin:-23px 0 -29px"), -16, -16, 0, 0, -16, -12),
        ("before-mixed", dict(child="margin:23px 0 -29px"), -6, -6, 0, 0, -6, -6),
        ("before-clear", dict(child="clear:left"), 13, 61, 0, 54, 13, 84),
        ("before-unmatched-clear", dict(child="clear:right"), 29, 29, 0, 0, 29, 29),
        ("before-height", dict(child="height:10px"), 23, 23, 10, 10, 23, 62),
        ("before-padding", dict(child="padding-top:5px"), 23, 23, 5, 5, 23, 57),
        ("before-minimum", dict(child="min-height:20px"), 23, 23, 20, 20, 23, 72),
        ("before-parent-padding", dict(style="padding-top:5px"), 13, 41, 0, 5, 18, 47),
        ("before-parent-bottom", dict(style="padding-bottom:7px"), 29, 29, 0, 7, 29, 53),
        ("before-parent-minimum", dict(style="min-height:20px"), 29, 29, 0, 20, 29, 66),
        ("before-line", dict(text_after=True, style="font:13px/20px sans-serif"), 29, 29, 0, 20, 29, 66),
    ):
        expected = dict(empty=(top, parent_height), inner=(child_top, child_height), floating=(float_top, 48))
        if after is not None:
            expected["after"] = (after, 8)
        if options.get("wrap"):
            expected["wrapper"] = (top, parent_height)
        if options.get("float_after_child"):
            expected["second"] = (float_top, 20)
        yield name, dict(child="") | options, expected


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><title>Float-only margins</title><style>',
             'body{margin:0}.case{display:flow-root;width:300px;border:1px solid;margin:10px}',
             'ol{margin:0;padding:0}li{list-style:none}</style>']
    for name, c, _ in cases():
        def element(role, inside="", style="", tag="div"):
            return f'<{tag} id="{name}-{role}" style="{style}">{inside}</{tag}>'

        floating = "" if c.get("no_float") else element(
            "floating", style=f'float:{c.get("side", "left")};width:40px;height:48px;background:#c66')
        if c.get("two"):
            floating += element("second", style=f'float:left;width:{c.get("second_width", 280)}px;'
                                f'clear:{c.get("second_clear", "none")};height:20px;background:#66c')
        if c.get("inner_clear"):
            floating += element("cleared", style="clear:both;margin:11px 0 19px")
        if c.get("break_clear"):
            floating += f'<br style="clear:{c["break_clear"]}">'
        inside = element("inner", floating, c.get("inner", "")) if c.get("nested") else floating
        if "child" in c:
            inside += element("inner", style="margin:23px 0 29px;" + c["child"])
        if c.get("float_after_child"):
            inside += element("second", style="float:left;width:40px;height:20px;background:#66c")
        if c.get("text_after"):
            inside += "text"
        empty = element("empty", inside,
                        f'margin:{c.get("top", 13)}px 0 {c.get("bottom", 17)}px;{c.get("style", "")}',
                        c.get("tag", "div"))
        if c.get("wrap"):
            empty = element("wrapper", empty)
        if c.get("tag") == "li":
            empty = "<ol>" + empty + "</ol>"
        after = "" if c.get("last") else element(
            "after", style=f'height:8px;margin-top:{c.get("after", 0)}px;clear:{c.get("clear", "none")};background:#6a6')
        prefix = element("prefix", style=f'height:{c["prefix"]}px;margin-bottom:{c.get("previous", 0)}px') \
            if "prefix" in c else ""
        if c.get("preceding"):
            prefix += element("preceding", style="float:left;width:40px;height:16px;background:#cc6")
        parts.append(f'<section class="case" id="{name}-case">{prefix}{empty}{after}</section>')
    return "\n".join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-float-margins-") as tmp:
        fixture = Path(tmp) / "float-margins.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x800",
                "--capture-settle", "100", "--capture-timeout", "8000",
                "--capture-resize", "500x900", "--capture-resize", "390x800", "--enable_javascript=0"]
        for name, _, expected in cases():
            for suffix in ("case", *expected):
                args += ["--capture-id", name + "-" + suffix]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
        assert process.returncode == 0, process.stderr
        assert "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 3, len(steps)
        for index, report in enumerate(steps):
            assert all(e["status"] == "ok" and e.get("boxes") for e in report["elements"]), report["elements"]
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
            for name, _, expected in cases():
                case = boxes[name + "-case"]
                for suffix, geometry in expected.items():
                    box = boxes[name + "-" + suffix]
                    actual = box["y"] - case["y"] - 1, box["height"]
                    assert actual == geometry, (index, name, suffix, actual, geometry)
                # The flow-root contains the final in-flow edge and both floats.
                height = max(0, *(top + height for top, height in expected.values())) + 2
                assert case["height"] == height, (index, name, case, height)
        assert steps[0]["elements"] == steps[2]["elements"], "Resize round trip changed geometry"
    print(f"Float-only margins: {len(list(cases()))} cases passed before and after resizing.")


if __name__ == "__main__":
    main()
