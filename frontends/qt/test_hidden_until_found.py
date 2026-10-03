#!/usr/bin/env python3
"""Native until-found rendering, fragment navigation, search and teardown."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/hidden-until-found.html"
ids = ("visible", "outer", "nested", "target", "second", "second-text", "fixed-size", "minimum",
       "flex", "flex-child", "grid", "grid-child", "override", "override-text", "display-override",
       "display-text", "inline", "none", "blocked", "blocked-target", "css-hidden", "css-text",
       "wrapped", "transformed", "after", "float-hidden", "image", "image-sized", "input", "textarea",
       "select", "row", "cell-visible", "cell", "cell-text", "inherit", "initial", "unset", "noninherited", "variable")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def capture(directory, actions=(), fragment="", page=fixture):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x500",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args.extend(("--capture-id", key))
    for action, value in actions:
        args.extend(("--capture-" + action, value))
    process = subprocess.run([*args, page.as_uri() + fragment], env=env,
                             capture_output=True, text=True, timeout=20)
    assert process.returncode == 0, process.stderr
    assert "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    assert not meta["javascript"], meta
    raw = json.loads((directory / "steps.json").read_text()) if actions else [json.loads((directory / "inspection.json").read_text())]
    return [{e["id"]: e for e in step["elements"]} for step in raw]


def box(step, key):
    assert step[key]["boxes"], f"Missing box: {key}"
    return step[key]["boxes"][0]


def revealed(step, *keys):
    for key in keys:
        assert step[key]["state"]["hidden"] is None, key
        assert not box(step, key)["contents_skipped"], key


def highlighted(step, key, count=1):
    assert box(step, key)["highlighted_fragments"] == count, (key, box(step, key))


with tempfile.TemporaryDirectory(prefix="wisp-until-found-") as tmp:
    root = Path(tmp)
    ordinary = root / "ordinary.html"
    ordinary.write_text('<!doctype html><p id="visible">needle ordinary page</p>')
    ordinary_steps = capture(root / "ordinary", [("find", "needle")], page=ordinary)
    highlighted(ordinary_steps[-1], "visible")  # Existing rendered-box search and close.
    actions = [("find-all", "needle"), ("find", "needle"), ("find", "needle"),
               ("find", "needle"), ("find-back", "needle"), ("find", ""),
               ("find", "beta gamma"), ("find-case", "MIXED CASE")]
    steps = capture(root / "search", actions)
    initial = steps[0]
    for key, height in (("outer", 20), ("second", 20), ("fixed-size", 50), ("minimum", 60),
                        ("flex", 20), ("grid", 20), ("display-override", 0), ("css-hidden", 0)):
        b = box(initial, key)
        assert b["contents_skipped"] and b["child_count"] == 0, (key, b)
        assert b["border_box"]["height"] == height, (key, b)
    for key in ("image", "image-sized", "input", "textarea", "select", "cell", "inherit", "variable"):
        assert box(initial, key)["contents_skipped"], key
    for key in ("row", "cell-visible", "initial", "unset", "noninherited"):
        assert not box(initial, key)["contents_skipped"], key
    assert not initial["cell-text"]["boxes"]
    for key, size in (("image", (4, 4)), ("image-sized", (64, 34))):
        r = box(initial, key)["border_box"]
        assert (r["width"], r["height"]) == size, (key, r)
    assert box(initial, "float-hidden")["border_box"]["x"] == 100
    assert not box(initial, "override")["contents_skipped"]
    assert box(initial, "override-text")["border_box"]["height"] == 20
    assert not box(initial, "inline")["contents_skipped"]
    for step in steps:
        for key in ("none", "blocked", "blocked-target", "css-text", "flex-child", "grid-child", "display-text"):
            assert not step[key]["boxes"], key
    highlighted(steps[1], "visible")
    assert box(steps[1], "outer")["contents_skipped"]  # Show-all must not reveal inactive matches.
    revealed(steps[2], "outer", "nested")
    highlighted(steps[2], "target")
    assert box(steps[2], "second")["contents_skipped"]
    revealed(steps[3], "second")
    highlighted(steps[3], "second-text")
    highlighted(steps[4], "second-text")  # Hidden barriers must not add further matches.
    highlighted(steps[5], "target")
    for key in ("visible", "target", "second-text"):
        highlighted(steps[6], key, 0)
    revealed(steps[7], "wrapped")
    highlighted(steps[7], "wrapped", 2)  # Match crosses two independently laid-out line fragments.
    revealed(steps[8], "transformed")
    highlighted(steps[8], "transformed")  # Close with a live highlight to exercise cleanup.

    clicked = capture(root / "fragment", [("click-id", "blocked-fragment"), ("click-id", "fragment")])
    assert box(clicked[1], "outer")["contents_skipped"]
    revealed(clicked[2], "outer", "nested")
    for name, target in (("nested-fragment", "target"), ("self-fragment", "outer")):
        # An action also gives us a before/after snapshot for initial fragment navigation.
        step = capture(root / name, [("scroll", "0,0")], "#" + target)[0]
        revealed(step, "outer")
        if target == "target":
            revealed(step, "nested")
        else:
            assert box(step, "nested")["contents_skipped"]

    # Replace or clear a query before its scheduled reveal rebuild has run.
    for name, final in (("clear", ""), ("replace", "MIXED CASE")):
        steps = capture(root / name, [("batch", "2"), ("find", "beta gamma"), ("find", final)])
        revealed(steps[-1], "wrapped")
        if final:
            revealed(steps[-1], "transformed")
            highlighted(steps[-1], "transformed")
        else:
            highlighted(steps[-1], "wrapped", 0)
print("Until-found: box containment, nested fragments, search order/highlights and cancellation passed.")
