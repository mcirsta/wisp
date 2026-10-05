#!/usr/bin/env python3
"""HTML details name-group insertion/open rules through native reading paths."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", QT_FONT_DPI="96")
# Expectations come from HTML's details attribute-change/insertion steps:
# https://html.spec.whatwg.org/multipage/interactive-elements.html#the-details-element
markup = '''<!doctype html><meta charset="utf-8">
<style>body { font:16px/24px sans-serif } details { margin:8px } p { margin:0 }</style>
<details id="first" name="news" open><summary id="first-summary">First</summary>
<p id="first-text">first-token</p></details>
<div><details id="second" open name="news"><summary id="second-summary">Second</summary>
<p id="second-text">second-token</p></details></div>
<details id="third" name="news">Default legend<p id="third-text">third-token</p></details>
<details id="independent" name="News" open><summary>Independent</summary><p>Case-sensitive name</p></details>
<details id="empty-one" name="" open><summary>Empty one</summary><p>Not grouped</p></details>
<details id="empty-two" name="" open><summary>Empty two</summary><p>Not grouped</p></details>
<a id="fragment" href="#first-text">Reveal first</a>
'''
ids = ("first", "second", "third", "first-text", "second-text", "third-text",
       "independent", "empty-one", "empty-two")


def capture(root, page, actions, javascript):
    args = [str(binary), "--capture-dir", str(root), "--capture-size", "700x600",
            "--capture-settle", "50", "--capture-timeout", "12000",
            f"--enable_javascript={javascript}"]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    result = subprocess.run([*args, page.as_uri()], env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and "runtime error:" not in result.stderr, result.stderr
    meta = json.loads((root / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    return [{e["id"]: e for e in step["elements"]}
            for step in json.loads((root / "steps.json").read_text())]


def check(step, active):
    for key in ("first", "second", "third"):
        expected = key == active
        assert step[key]["state"]["open"] == expected, (key, active, step[key])
        assert bool(step[key + "-text"]["boxes"]) == expected, (key, active)
    for key in ("independent", "empty-one", "empty-two"):
        assert step[key]["state"]["open"], key


with tempfile.TemporaryDirectory(prefix="wisp-details-groups-") as tmp:
    root = Path(tmp)
    page = root / "groups.html"
    page.write_text(markup)
    for js in (0, 1):
        steps = capture(root / f"click-{js}", page,
                        [("click-id", "second-summary"), ("click-id", "second-summary"),
                         ("click-id", "first-summary"), ("resize", "450x600")], js)
        for step, active in zip(steps, ("first", "second", None, "first", "first"), strict=True):
            check(step, active)
        steps = capture(root / f"find-{js}", page,
                        [("find", "second-token"), ("find", "third-token"),
                         ("click-id", "fragment")], js)
        for step, active in zip(steps, ("first", "second", "third", "first"), strict=True):
            check(step, active)
        for step, key in ((steps[1], "second-text"), (steps[2], "third-text")):
            assert step[key]["boxes"][0]["highlighted_fragments"] == 1, (key, step[key])
print("Details name groups: parser, native activation, find/fragment reveal and resizing passed.")
