#!/usr/bin/env python3
"""HTML image representation and CSS Pseudo 4, not browser-specific pixel baselines.

https://html.spec.whatwg.org/multipage/rendering.html#images-3
https://drafts.csswg.org/css-pseudo-4/#generated-content
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/image-fallback.html"
ids = ("self-hidden", "visible", "hidden", "hidden-image", "closed", "closed-image", "spaces", "wrapped", "wrapped-reference",
       "pre", "pre-reference", "inline-parent", "inline", "inline-reference", "empty", "no-alt", "valid", "srcset",
       "failed", "failed-image", "expected", "unrequested", "loaded", "loaded-image")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def capture(directory, actions, source=fixture, options=()):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x700",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0", *options]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    run = subprocess.run([*args, str(source)], env=env, capture_output=True, text=True, timeout=30)
    assert run.returncode == 0 and "runtime error:" not in run.stderr, run.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((directory / "steps.json").read_text())
    for index, step in enumerate(steps):
        assert step["active_resources"] == 0, (directory.name, index, step["active_resources"])
        assert step["resource_count"] == steps[0]["resource_count"], (directory.name, index, step["resource_count"])
    return steps


def elements(step):
    return {e["id"]: e for e in step["elements"]}


def box(step, name):
    item = elements(step)[name]
    assert item["boxes"], (name, item)
    return item["boxes"][0]


def counts(step, name, total, before=0, after=0):
    b = box(step, name)
    actual = tuple(b[k] for k in ("highlighted_fragments", "highlighted_before_fragments", "highlighted_after_fragments"))
    assert actual == (total, before, after), (name, actual, (total, before, after))


with tempfile.TemporaryDirectory(prefix="wisp-image-fallback-") as tmp:
    root = Path(tmp)
    steps = capture(root / "fallback", [("find", "visible-before"), ("find", "visible-alt"),
        ("find", "visible-after"), ("find", "hidden-before"), ("find", "hidden-alt"),
        ("find", "hidden-after"), ("find", "closed-alt"), ("find", "spaces-alt"),
        ("find", "beta gamma"), ("find-case", "SECOND"), ("find", "inline-alt"),
        ("click-id", "failed-summary"), ("find", "failed-before"), ("find", "failed-alt"),
        ("find", "failed-after"), ("resize", "760x700"), ("find", "failed-alt"), ("find", "self-alt")])
    for i, name, total, before, after in ((1, "visible", 1, 1, 0), (2, "visible", 1, 0, 0),
        (3, "visible", 1, 0, 1), (4, "hidden-image", 1, 1, 0), (5, "hidden-image", 1, 0, 0),
        (6, "hidden-image", 1, 0, 1), (7, "closed-image", 1, 0, 0), (8, "spaces", 1, 0, 0),
        (9, "wrapped", 2, 0, 0), (10, "pre", 1, 0, 0), (11, "inline-parent", 1, 0, 0),
        (13, "failed-image", 1, 1, 0), (14, "failed-image", 1, 0, 0), (15, "failed-image", 1, 0, 1),
        (17, "failed-image", 1, 0, 0), (18, "self-hidden", 1, 0, 0)):
        counts(steps[i], name, total, before, after)
    assert elements(steps[18])["self-hidden"]["state"]["hidden"] is None
    assert not elements(steps[0])["hidden-image"]["boxes"]
    assert elements(steps[4])["hidden"]["state"]["hidden"] is None
    assert elements(steps[7])["closed"]["state"]["open"]
    assert not elements(steps[12])["failed"]["state"]["open"]
    assert elements(steps[13])["failed"]["state"]["open"]
    for name in ("wrapped", "pre", "inline"):
        actual = box(steps[0], name)["border_box"]
        reference = box(steps[0], name + "-reference")["border_box"]
        assert (actual["width"], actual["height"]) == (reference["width"], reference["height"]), (name, actual, reference)

    steps = capture(root / "excluded", [("click-id", "loaded-summary"), ("find-all", "forbidden"),
        ("find", "unrequested-alt"), ("find", "loaded-alternative")])
    for step in steps[2:]:
        for name in ("empty", "no-alt", "valid", "srcset"):
            counts(step, name, 0)
        assert elements(step)["expected"]["state"]["hidden"] == "until-found"
        assert not elements(step)["loaded"]["state"]["open"]

    ordinary = root / "ordinary.html"
    ordinary.write_text('<!doctype html><style>img{display:block}img::before{content:"ordinary-before"}</style>'
                        '<img id="visible" alt="ordinary-alt">')
    steps = capture(root / "ordinary", [("find", "ordinary-alt"), ("find", "ordinary-before")], source=ordinary)
    counts(steps[1], "visible", 1)
    counts(steps[2], "visible", 1, before=1)

    # Dimensions are readable, but JPEG pixel conversion fails after loading.
    late = root / "late.html"
    late.write_text('<!doctype html><style>img{display:block}img::before{content:"late-before"}</style>'
                    '<details id="failed" open><summary id="failed-summary">Late failure</summary>'
                    '<img id="failed-image" width="80" height="20" alt="late-alternative" '
                    'src="data:image/jpeg;base64,/9j/wAALCAABAAEBAREA/9oACAEBAAA/AP/Z"></details>')
    for cache in (0, 33554432):
        steps = capture(root / f"late-{cache}", [("click-id", "failed-summary"),
            ("find", "late-before"), ("find", "late-alternative")], source=late,
            options=(f"--image_cache_size={cache}",))
        assert not elements(steps[1])["failed"]["state"]["open"]
        assert elements(steps[2])["failed"]["state"]["open"]
        counts(steps[2], "failed-image", 1, before=1)
        counts(steps[3], "failed-image", 1)

    quirks = root / "quirks.html"
    quirks.write_text('<style>img::before{content:"quirks-pseudo"}</style>'
                      '<details id="closed"><summary>Quirks dimensions</summary>'
                      '<img id="closed-image" alt="quirks-alt" width="80" height="25"></details>'
                      '<img id="visible" alt="visible-alt" width="80" height="25">')
    steps = capture(root / "quirks", [("find", "quirks-pseudo"), ("find", "quirks-alt")], source=quirks)
    for step in steps:
        counts(step, "visible", 0)
        assert not elements(step)["closed"]["state"]["open"]

    steps = capture(root / "disabled", [("find", "valid-alternative"), ("find", "srcset-alternative"),
        ("find", "unrequested-alt")], options=("--foreground_images=0",))
    counts(steps[1], "valid", 1)
    counts(steps[2], "srcset", 1)
    counts(steps[3], "unrequested", 1)
    assert elements(steps[3])["expected"]["state"]["hidden"] is None

print("Image fallback: layout, generated text, search/reveal, failures and disabled images passed.")
