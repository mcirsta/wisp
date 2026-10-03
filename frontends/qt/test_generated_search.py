#!/usr/bin/env python3
"""Generated text search identity, order, reveal, wrapping and cancellation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/generated-search.html"
ids = ("first", "outer", "second", "attribute", "quoted", "wrapped", "nested-outer", "nested",
       "inline-parent", "none", "invisible", "skipped", "ordinary-hidden", "image", "pseudo-visible", "toggle", "label",
       "flex", "flex-child", "grid", "grid-child", "canvas-outer", "canvas", "replaced-outer")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def capture(directory, actions):
    args = [str(binary), "--capture-dir", str(directory), "--capture-size", "700x500",
            "--capture-settle", "50", "--capture-timeout", "12000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    for action, value in actions:
        args += ["--capture-" + action, value]
    result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0 and "runtime error:" not in result.stderr, result.stderr
    meta = json.loads((directory / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    return [{e["id"]: e for e in s["elements"]} for s in json.loads((directory / "steps.json").read_text())]


def counts(step, name, total, before=0, after=0):
    assert step[name]["boxes"], (name, step[name])
    box = step[name]["boxes"][0]
    actual = tuple(box[k] for k in ("highlighted_fragments", "highlighted_before_fragments", "highlighted_after_fragments"))
    assert actual == (total, before, after), (name, actual, (total, before, after))


with tempfile.TemporaryDirectory(prefix="wisp-generated-search-") as tmp:
    root = Path(tmp)
    steps = capture(root / "order", [("find", "needle")] * 6 + [("find-back", "needle"), ("find-all", "needle")])
    for i, name in ((1, "first"), (4, "second")):
        counts(steps[i], name, 1, before=1)
        counts(steps[i + 1], name, 1)
        counts(steps[i + 2], name, 1, after=1)
    assert steps[3]["outer"]["state"]["hidden"] == "until-found"
    assert steps[4]["outer"]["state"]["hidden"] is None
    counts(steps[7], "second", 1)
    counts(steps[8], "first", 3, 1, 1)
    counts(steps[8], "second", 3, 1, 1)

    steps = capture(root / "content", [("find-case", "attribute-token"), ("find", '"quotation"'),
        ("find", "beta gamma"), ("find", "nested-token"), ("find", "inline-token"),
        ("find", "visibility-override"), ("find", ""), ("click-id", "label")])
    counts(steps[1], "attribute", 1, before=1)
    counts(steps[2], "quoted", 3, after=3)
    assert steps[3]["wrapped"]["state"]["hidden"] is None
    counts(steps[3], "wrapped", 2, before=2)
    assert steps[4]["nested-outer"]["state"]["hidden"] is None
    counts(steps[4], "nested", 1, after=1)
    counts(steps[5], "inline-parent", 1, before=1)
    counts(steps[6], "pseudo-visible", 1, before=1)
    counts(steps[7], "pseudo-visible", 0)
    assert steps[8]["toggle"]["state"]["checked"]

    steps = capture(root / "formatting", [("find", "flex-token"), ("find", "grid-token"), ("find", "canvas-token")])
    for i, owner, child, before, after in ((1, "flex", "flex-child", 1, 0),
                                         (2, "grid", "grid-child", 0, 1), (3, "canvas-outer", "canvas", 1, 0)):
        assert not steps[0][child]["boxes"], child
        assert steps[i][owner]["state"]["hidden"] is None, owner
        # Inline canvas fallback text is flattened into the parent's inline container.
        counts(steps[i], owner if child == "canvas" else child, 1, before, after)

    # Excluded pseudo boxes must not become matches or reveal ordinary hidden content.
    steps = capture(root / "excluded", [("find-all", "forbidden")])
    for name in ("none", "invisible", "skipped", "image"):
        counts(steps[-1], name, 0)
    assert steps[-1]["ordinary-hidden"]["state"]["hidden"] is not None
    assert steps[-1]["replaced-outer"]["state"]["hidden"] == "until-found"

    for name, query in (("clear", ""), ("replace", "attribute-token")):
        steps = capture(root / name, [("batch", "2"), ("find", "beta gamma"), ("find", query)])
        counts(steps[-1], "wrapped", 0)
        if query:
            counts(steps[-1], "attribute", 1, before=1)
print("Generated search: source identity, DOM order, reveal, strings/attributes/quotes, wrapping and cancellation passed.")
