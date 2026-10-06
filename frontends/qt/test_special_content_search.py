#!/usr/bin/env python3
"""Representation controls rendered/searchable descendants, before and after boxing.

Expectations follow HTML iframe descendants, canvas/noscript representation,
HTML rendering 15.4.1, and CSS Pseudo 4's replaced-parent suppression:
https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-iframe-element
https://html.spec.whatwg.org/multipage/canvas.html#the-canvas-element
https://html.spec.whatwg.org/multipage/scripting.html#the-noscript-element
https://drafts.csswg.org/css-pseudo-4/#generated-content
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/special-content-search.html"
ids = ("frame-visible", "frame-inline", "frame-empty", "frame-invisible", "embed-visible", "frame-hidden", "frame-hidden-owner",
       "frame-closed", "frame-closed-owner", "frame-loaded", "frame-loaded-owner", "embed-hidden", "embed-hidden-owner",
       "canvas-visible", "canvas-visible-text", "canvas-hidden", "canvas-hidden-owner", "canvas-hidden-text",
       "canvas-closed", "canvas-closed-owner", "canvas-closed-text", "noscript-visible", "noscript-hidden",
       "noscript-hidden-owner", "noscript-closed", "noscript-closed-owner", "normal-hidden", "normal-button",
       "normal-object-text")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def capture(directory, scripting, actions):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x700", "--capture-settle", "50",
            "--capture-timeout", "12000", f"--enable_javascript={scripting}"]
    for name in ids:
        args += ["--capture-id", name]
    for action, value in actions:
        args += ["--capture-" + action, value]
    run = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
    assert run.returncode == 0 and "runtime error:" not in run.stderr, run.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((directory / "steps.json").read_text())
    for i, step in enumerate(steps):
        assert step["active_resources"] == 0, (scripting, i, step["active_resources"])
    return [{e["id"]: e for e in s["elements"]} for s in steps]


def highlights(step, name, count, before=0, after=0):
    assert step[name]["boxes"], (name, step[name])
    b = step[name]["boxes"][0]
    actual = tuple(b[k] for k in ("highlighted_fragments", "highlighted_before_fragments", "highlighted_after_fragments"))
    assert actual == (count, before, after), (name, actual, (count, before, after))


with tempfile.TemporaryDirectory(prefix="wisp-special-search-") as tmp:
    root = Path(tmp)
    for scripting in (0, 1):
        steps = capture(root / f"excluded-{scripting}", scripting, [("click-id", "loaded-summary"),
            ("find", "forbidden-hidden"), ("find", "forbidden-closed"), ("find", "forbidden-loaded"),
            ("find-all", "forbidden-pseudo"), ("find", "forbidden-frame")])
        rect = steps[0]["frame-inline"]["boxes"][0]["border_box"]
        assert (rect["width"], rect["height"]) == (100, 30), rect
        for step in steps[2:]:
            assert step["frame-hidden"]["state"]["hidden"] == "until-found"
            assert step["embed-hidden"]["state"]["hidden"] == "until-found"
            for name in ("frame-closed", "frame-loaded"):
                assert not step[name]["state"]["open"], name
            for name in ("frame-visible", "frame-inline", "frame-empty", "frame-invisible", "embed-visible"):
                highlights(step, name, 0)

    # With scripting enabled, bitmap canvas and noscript descendants cannot
    # supply visible matches or trigger ancestor revealing.
    steps = capture(root / "scripted", 1, [("find", "canvas-hidden"), ("find", "canvas-closed"),
        ("find-all", "canvas-before"), ("find", "noscript-hidden"), ("find", "noscript-closed"),
        ("find-all", "noscript-before"), ("find", "button-token"), ("find", "object-token")])
    for step in steps:
        assert not step["canvas-visible-text"]["boxes"]
        assert not step["noscript-visible"]["boxes"]
        assert step["canvas-hidden"]["state"]["hidden"] == "until-found"
        assert step["noscript-hidden"]["state"]["hidden"] == "until-found"
        assert not step["canvas-closed"]["state"]["open"]
        assert not step["noscript-closed"]["state"]["open"]
    highlights(steps[7], "normal-button", 1)
    highlights(steps[8], "normal-object-text", 1)

    # Without scripting those same owners represent real fallback text.
    steps = capture(root / "fallback", 0, [("find", "canvas-hidden"), ("find", "canvas-closed"),
        ("find", "canvas-before"), ("find", "canvas-after"), ("find", "noscript-hidden"),
        ("find", "noscript-closed"), ("find", "noscript-before"), ("find", "noscript-after")])
    for index, name in ((1, "canvas-hidden-text"), (2, "canvas-closed-text"),
                        (5, "noscript-hidden-owner"), (6, "noscript-closed-owner")):
        highlights(steps[index], name, 1)
    for index, name, before, after in ((3, "canvas-visible", 1, 0), (4, "canvas-visible", 0, 1),
                                       (7, "noscript-visible", 1, 0), (8, "noscript-visible", 0, 1)):
        highlights(steps[index], name, 1, before, after)
    assert steps[1]["canvas-hidden"]["state"]["hidden"] is None
    assert steps[2]["canvas-closed"]["state"]["open"]
    assert steps[5]["noscript-hidden"]["state"]["hidden"] is None
    assert steps[6]["noscript-closed"]["state"]["open"]

print("Embedded/script-dependent content: rendering, generated text, search and reveal agree.")
