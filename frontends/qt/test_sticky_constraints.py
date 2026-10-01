#!/usr/bin/env python3
"""Settled sticky constraints: scrollports, exclusions and retained lifetimes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
fixture = Path(__file__).resolve().parents[2] / "test/sticky-constraints.html"
ids = ("scope", "updated", "replaced", "untouched", "viewport-header", "hidden", "hidden-content",
       "hidden-header", "outer", "inner", "inner-header", "bars", "bars-content", "bars-header",
       "inline-container", "inline-header", "flex", "flex-header", "grid", "grid-header", "rtl", "rtl-header", "logical-container", "logical",
       "nested", "nested-header", "ordinary-inline", "float-header", "float-container", "in-float",
       "transform-header", "vertical-header", "auto")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def run(out, page, actions=(), extra=()):
    args = [binary, "--capture-dir", str(out), "--capture-size", "800x600", "--capture-settle", "50",
            "--capture-timeout", "15000", *extra]
    for key in ids:
        args += ["--capture-id", key]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((out / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    return json.loads((out / ("steps.json" if actions else "inspection.json")).read_text())


def box(step, key):
    element = next(e for e in step["elements"] if e["id"] == key)
    assert element["status"] == "ok" and element.get("boxes"), element
    return element["boxes"][0]


def constraint(step, key, cb, port=None):
    sticky = box(step, key)["sticky"]
    assert sticky["status"] == "prepared", (key, sticky)
    assert sticky["containing_block"]["node"]["id"] == cb, (key, sticky)
    if port:
        assert sticky["scrolling_ancestor"]["node"]["id"] == port, (key, sticky)
    else:
        assert sticky["scrolling_ancestor"] == {"viewport": True}, (key, sticky)
    return sticky


def geometry(step):
    # Stable, pointer-free summary for comparison with full reconstruction.
    return {e["id"]: [(b["border_box"], b.get("sticky")) for b in e["boxes"]] for e in step["elements"]}


with tempfile.TemporaryDirectory(prefix="wisp-sticky-constraints-") as directory:
    tmp = Path(directory)
    initial = run(tmp / "initial", fixture)
    assert initial["sticky_layout_valid"], initial
    viewport = constraint(initial, "viewport-header", "untouched")
    assert viewport["y"]["scrollport_size"] == 600 and viewport["y"]["inset_start"] == 60, viewport
    hidden = constraint(initial, "hidden-header", "hidden-content", "hidden")
    assert hidden["y"]["scrollport_size"] == 128 and hidden["y"]["inset_start"] == 15, hidden
    assert hidden["x"]["scrollport_size"] == 208 and hidden["x"]["inset_start"] == 20, hidden
    assert (hidden["y"]["margin_start"], hidden["y"]["margin_end"]) == (5, -2), hidden
    inner = constraint(initial, "inner-header", "inner", "inner")
    assert inner["y"]["scrollport_size"] == 60 and inner["y"]["inset_start"] == 6, inner
    assert box(initial, "inner")["scrollbars"] == {"horizontal": False, "vertical": False, "x": 0, "y": 0}
    bars = constraint(initial, "bars-header", "bars-content", "bars")
    padding = box(initial, "bars")["padding_box"]
    assert (bars["x"]["scrollport_size"], bars["y"]["scrollport_size"]) == (padding["width"] - 16, padding["height"] - 16), bars
    for key, cb in (("inline-header", "inline-container"), ("flex-header", "flex"), ("grid-header", "grid"),
                    ("nested-header", "nested"), ("in-float", "float-container")):
        constraint(initial, key, cb)
    logical = constraint(initial, "logical", "logical-container")
    assert not logical["x"]["reverse"] and logical["x"]["inset_start"] == 7 and logical["x"]["inset_end"] is None, logical
    rtl = constraint(initial, "rtl-header", "rtl")
    assert rtl["x"]["reverse"] and rtl["x"]["inset_start"] is None and rtl["x"]["inset_end"] == 7, rtl
    assert logical["y"]["inset_start"] == 60, logical
    for key, reason in (("ordinary-inline", "box_type"), ("float-header", "floated_sticky"),
                        ("transform-header", "transform"), ("vertical-header", "writing_mode")):
        assert box(initial, key)["sticky"] == {"status": "unsupported", "reason": reason}, key
    assert box(initial, "auto")["sticky"] == {"status": "auto"}

    # Root/body overflow propagation: hidden body belongs to the viewport only
    # while the root's two axes are visible. A mixed-axis root blocks propagation.
    for name, css, port in (("body", "body {overflow:hidden}", None),
                            ("root", "html {overflow-y:hidden} body {overflow:hidden}", "body")):
        page = tmp / f"{name}.html"
        page.write_text(fixture.read_text().replace(
            '<section id="region">', f'<style>{css}</style><body id="body"><section id="region">'))
        step = run(tmp / name, page)
        constraint(step, "viewport-header", "untouched", port)

    actions = [("click-label", "change"), ("click-label", "change")] * 4
    actions += [("resize", "640x480"), ("resize", "800x600"), ("scroll", "0,80"), ("click-label", "change"),
                ("click-label", "change"), ("scroll", "0,0")]
    full = run(tmp / "full", fixture, actions, ("--capture-force-full-restyle",))
    for name, flags in (("retained", ()), ("uncached", ("--capture-no-inline-layout-reuse", "--capture-no-intrinsic-reuse"))):
        steps = run(tmp / name, fixture, actions, flags)
        assert len(steps) == len(full) == len(actions) + 1
        for i, (step, reference) in enumerate(zip(steps, full)):
            assert step["sticky_layout_valid"], (name, i)
            assert geometry(step) == geometry(reference), (name, i)
            height = 160 if (i <= 8 and i % 2) or i == 12 else 100
            updated = constraint(step, "updated", "scope", "scope")
            assert updated["y"]["scrollport_size"] == height, (name, i, updated)
            assert updated["y"]["inset_start"] == height // 10 + 3, (name, i, updated)
            if 3 <= i <= 8:
                assert step["tree_allocations"] == steps[3 + (i - 3) % 2]["tree_allocations"], (name, i)
                assert box(step, "viewport-header")["box_address"] == box(steps[2], "viewport-header")["box_address"], (name, i)
                if step.get("restyle_profile"):
                    assert step["restyle_profile"]["update_mode"] == "partial_boxes", (name, i, step["restyle_profile"])
        for before, after in ((10, 11), (13, 14)):
            # Scroll-only actions change visual queries, but must not
            # rewrite the prepared, unscrolled constraints or allocate records.
            for key in ("updated", "viewport-header", "hidden-header", "in-float"):
                for axis in ("x", "y"):
                    assert box(steps[before], key)["sticky"][axis] == box(steps[after], key)["sticky"][axis], (key, axis)
            assert steps[before]["tree_allocations"] == steps[after]["tree_allocations"], (name, before, after)
        assert (tmp / name / "page.png").read_bytes() == (tmp / "initial" / "page.png").read_bytes(), name

print("Sticky preparation: scrollports, geometry, exclusions, resize and retained/full parity passed.")
