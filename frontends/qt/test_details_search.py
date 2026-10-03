#!/usr/bin/env python3
"""Native disclosure rendering/activation and deferred search/fragment reveal."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/details-search.html"
ids = ("visible", "first", "first-summary", "summary-label", "first-text", "second-summary", "tail",
       "outer", "outer-summary", "until", "inner", "inner-summary", "deep-text", "generated",
       "until-outer", "mixed", "mixed-text", "preopen", "preopen-text", "reordered", "before-summary",
       "late-summary", "after-summary", "fallback", "fallback-text", "blocked", "blocked-text",
       "ordinary-hidden", "contents-hidden", "interactive", "interactive-text", "checkbox", "far", "far-text",
       "flex-details", "flex-summary", "flex-one", "flex-two")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")


def capture(directory, actions, fragment="", page=fixture):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x500",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    process = subprocess.run([*args, page.as_uri() + fragment], env=env, capture_output=True, text=True, timeout=25)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    return [{e["id"]: e for e in s["elements"]} for s in json.loads((directory / "steps.json").read_text())]


def opened(step, *names):
    for name in names:
        assert step[name]["state"]["open"], name


def closed(step, *names):
    for name in names:
        assert not step[name]["state"]["open"], name


def highlights(step, name, count=1):
    boxes = step[name]["boxes"]
    assert boxes and boxes[0]["highlighted_fragments"] == count, (name, boxes)


with tempfile.TemporaryDirectory(prefix="wisp-details-") as tmp:
    root = Path(tmp)
    # No until-found element: details alone must select DOM-backed discovery.
    plain = root / "plain.html"
    plain.write_text('<!doctype html><details id="first"><summary id="first-summary">Summary</summary>'
                     '<p id="first-text">needle</p></details>')
    steps = capture(root / "plain", [("find", "needle"), ("click-id", "first-summary"),
        ("find", "needle")], page=plain)
    assert not steps[0]["first-text"]["boxes"]
    opened(steps[1], "first")
    highlights(steps[1], "first-text")
    closed(steps[2], "first")
    assert not steps[2]["first-text"]["boxes"]
    opened(steps[3], "first")
    highlights(steps[3], "first-text")

    steps = capture(root / "search", [("find", "summary-token"), ("find", "needle"), ("find", "needle"),
        ("find", "needle"), ("find-back", "needle"), ("find", "generated-token"),
        ("find", "mixed-token"), ("find", "far-token")])
    initial = steps[0]
    closed(initial, "first", "outer", "inner", "reordered", "fallback")
    opened(initial, "preopen")
    assert initial["first-summary"]["boxes"] and initial["preopen-text"]["boxes"]
    flex = {k: initial[k]["boxes"][0]["border_box"] for k in ("flex-summary", "flex-one", "flex-two")}
    assert flex["flex-one"]["x"] == flex["flex-two"]["x"] > flex["flex-summary"]["x"], flex
    assert flex["flex-two"]["y"] == flex["flex-one"]["y"] + flex["flex-one"]["height"], flex
    for key in ("first-text", "second-summary", "tail", "until", "inner", "deep-text", "before-summary",
                "after-summary", "fallback-text", "blocked-text"):
        assert not initial[key]["boxes"], key
    closed(steps[1], "first")
    highlights(steps[1], "first-summary")
    highlights(steps[2], "visible")
    closed(steps[2], "first", "outer")
    opened(steps[3], "first")
    highlights(steps[3], "first-text")
    opened(steps[4], "outer", "inner")
    assert steps[4]["until"]["state"]["hidden"] is None
    highlights(steps[4], "deep-text")
    highlights(steps[5], "first-text")
    highlights(steps[6], "generated")
    opened(steps[7], "mixed")
    assert steps[7]["until-outer"]["state"]["hidden"] is None
    highlights(steps[7], "mixed-text")
    opened(steps[8], "far")
    highlights(steps[8], "far-text")

    # Keep clicking the default legend, not the centre of the expanded contents.
    rect = initial["fallback"]["boxes"][0]["border_box"]
    legend = f'{rect["x"] + 12},{rect["y"] + rect["height"] // 2}'
    steps = capture(root / "activation", [("click-id", "summary-label"), ("click-id", "second-summary"),
        ("click-id", "first-summary"), ("click-point", legend), ("click-point", legend),
        ("click-id", "summary-link"), ("click-id", "checkbox-label"), ("click-id", "interactive-label")])
    opened(steps[1], "first")
    opened(steps[2], "first")  # Only the first summary toggles.
    closed(steps[3], "first")
    assert not steps[3]["first-text"]["boxes"]
    opened(steps[4], "fallback")
    closed(steps[5], "fallback")
    closed(steps[6], "interactive")  # Links and controls keep their own activation.
    closed(steps[7], "interactive")
    assert steps[7]["checkbox"]["state"]["checked"]
    opened(steps[8], "interactive")

    steps = capture(root / "order", [("find", "order-token")] * 3)
    closed(steps[1], "reordered")
    highlights(steps[1], "late-summary")
    opened(steps[2], "reordered")
    highlights(steps[2], "before-summary")
    highlights(steps[3], "after-summary")
    boxes = {k: steps[3][k]["boxes"][0]["border_box"] for k in ("late-summary", "before-summary", "after-summary")}
    assert boxes["late-summary"]["y"] < boxes["before-summary"]["y"] < boxes["after-summary"]["y"], boxes

    steps = capture(root / "fragments", [("click-id", "summary-fragment"), ("click-id", "self-fragment"),
        ("click-id", "blocked-fragment"), ("click-id", "fragment")])
    closed(steps[1], "first")
    closed(steps[2], "first")
    closed(steps[3], "blocked")
    opened(steps[4], "outer", "inner")
    opened(capture(root / "initial-fragment", [("scroll", "0,0")], "#deep-text")[0], "outer", "inner")

    steps = capture(root / "barriers", [("find-all", "forbidden")])
    closed(steps[-1], "blocked", "ordinary-hidden", "contents-hidden")
    steps = capture(root / "show-all", [("find-all", "needle")])
    closed(steps[-1], "first", "outer", "inner")
    highlights(steps[-1], "visible")

    for name, final in (("clear", ""), ("replace", "summary-token")):
        steps = capture(root / name, [("batch", "2"), ("find", "far-token"), ("find", final)])
        highlights(steps[-1], "far-text", 0)
        if final:
            closed(steps[-1], "first")
            highlights(steps[-1], "first-summary")
print("Details: collapse, native activation, summary order, nested reveal, barriers and cancellation passed.")
