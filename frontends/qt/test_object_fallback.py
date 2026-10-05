#!/usr/bin/env python3
"""Failed object requests represent their children (HTML object algorithm).

https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
CSS Pseudo 4 suppresses generated children only while the owner is replaced:
https://drafts.csswg.org/css-pseudo-4/#generated-content
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/object-fallback.html"
ids = ("panel", "summary", "missing", "missing-child", "nested", "nested-child",
       "corrupt", "corrupt-child", "unfetchable", "unfetchable-child", "valid", "valid-child")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def capture(directory, page, large=False):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x500",
            "--capture-settle", "50", "--capture-timeout", "20000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    actions = [("click-id", "summary"), ("find", "missing-generated-token"),
               ("click-id", "summary"), ("find", "corrupt-child-token"),
               ("click-id", "summary"), ("find", "unfetchable-generated-token"),
               ("click-id", "summary"), ("find", "nested-child-token"),
               ("click-id", "summary"), ("find", "forbidden"),
               ("click-id", "summary"), ("resize", "500x450")]
    if large:
        actions = [("resize", "500x450")]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    result = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and "runtime error:" not in result.stderr, result.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((directory / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, len(steps)
    for index, step in enumerate(steps):
        elements = {e["id"]: e for e in step["elements"]}
        opened = large or index in (0, 2, 4, 6, 8, 11, 12)
        assert elements["panel"]["state"]["open"] == opened, (index, elements["panel"])
        for key in ("missing-child", "nested-child", "corrupt-child", "unfetchable-child"):
            assert bool(elements[key]["boxes"]) == opened, (index, key, elements[key])
        assert not elements["valid-child"]["boxes"], (index, elements["valid-child"])
        assert step["active_resources"] == 0, (index, step["active_resources"])
        assert step["resource_count"] == steps[0]["resource_count"] == 5, (index, step["resource_count"])
        if not large and index in (2, 6):
            key = "missing" if index == 2 else "unfetchable"
            assert elements[key]["boxes"][0]["highlighted_before_fragments"] == 1, (index, elements[key])


with tempfile.TemporaryDirectory(prefix="wisp-object-fallback-") as tmp:
    root = Path(tmp)
    capture(root / "normal", fixture)
    # Also exercise a larger tree where initial/repeated construction yields.
    # The C test controls callback timing independently of machine speed.
    large = root / "large.html"
    large.write_text(fixture.read_text() + '<div>' + '<span>More text. </span>' * 2048 + '</div>')
    capture(root / "large", large, large=True)
print("Object fallback: missing, undecodable and unfetchable resources, nested fallback, retained state and search passed.")
