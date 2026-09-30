#!/usr/bin/env python3
"""Keep/clamp retained element offsets before painting, including off-screen boxes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/retained-scroll.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
ids = ("viewport", "untouched", "scope", "large", "medium", "small")


def run(out, page, actions, retained, reuse_lines=False):
    args = [str(binary), "--capture-dir", str(out), "--capture-size", "600x500",
            "--capture-settle", "50", "--capture-timeout", "30000"]
    if not retained:
        args.append("--capture-force-full-restyle")
    if reuse_lines:
        args.append("--capture-reuse-inline-layout")
    for key in ids:
        args += ["--capture-id", key]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=40)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((out / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((out / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, (len(steps), actions)
    return steps


def box(step, key):
    element = next(e for e in step["elements"] if e["id"] == key)
    assert element["boxes"], (key, step)
    return element["boxes"][0]


def offsets(step, key="viewport"):
    scroll = box(step, key)["scrollbars"]
    return scroll["x"], scroll["y"]


with tempfile.TemporaryDirectory(prefix="wisp-retained-scroll-") as directory:
    tmp = Path(directory)
    # Independent full constructions establish each maximum; no retained layout
    # or hand-copied scrollbar formula is used as the reference.
    reference_actions = [("scroll-element", "viewport,9999,9999"), ("click-label", "medium"),
                         ("scroll-element", "viewport,9999,9999"), ("click-label", "small")]
    full = run(tmp / "reference", fixture, reference_actions, False)
    maximum = {"large": offsets(full[1]), "medium": offsets(full[3])}
    assert all(a > b > c for a, b, c in zip(maximum["large"], maximum["medium"], (100, 80))), maximum
    assert offsets(full[4]) == (0, 0), full[4]

    actions = [("click-label", "medium"), ("click-label", "large"),
               ("scroll", "0,0"), ("scroll-element", "viewport,100,80"),
               ("scroll-element", "untouched,75,60"), ("click-label", "medium"),
               ("click-label", "large"), ("scroll-element", "viewport,9999,9999"),
               ("click-label", "medium"), ("click-label", "small"), ("click-label", "large")]
    for offscreen, reuse_lines in ((False, False), (True, False), (False, True), (True, True)):
        page = fixture
        if offscreen:
            source = fixture.read_text()
            labels = next(line for line in source.splitlines() if line.startswith('<div id="labels">'))
            page = tmp / "offscreen.html"
            page.write_text(source.replace(labels, "") + '<div style="height:900px"></div>' + labels)
        name = ("offscreen" if offscreen else "visible") + ("-lines" if reuse_lines else "")
        out = tmp / name
        steps = run(out, page, actions, True, reuse_lines)
        for i in range(4, len(steps)):
            for key in ("viewport", "untouched"):
                assert box(steps[i], key)["box_address"] == box(steps[2], key)["box_address"], (offscreen, i, key)
            if i >= 5:
                assert offsets(steps[i], "untouched") == (75, 60), (offscreen, i, offsets(steps[i], "untouched"))
        for i, expected in {4: (100, 80), 6: (100, 80), 7: (100, 80), 8: maximum["large"],
                            9: maximum["medium"], 10: (0, 0), 11: (0, 0)}.items():
            assert offsets(steps[i]) == expected, (offscreen, i, offsets(steps[i]), expected)
        for i in (6, 7, 9, 10, 11):
            if offscreen:
                viewport = box(steps[i], "viewport")["border_box"]
                assert viewport["y"] + viewport["height"] < steps[i]["scroll"]["y"], (i, steps[i]["scroll"])
            profile = steps[i].get("restyle_profile")
            if profile:
                assert profile["update_mode"] == "partial_boxes", (i, profile)
                if reuse_lines:
                    assert profile["inline_layout_reuses"] > 0, (i, profile)
        # Returning the viewport to view must not make a second, paint-dependent
        # correction. Compare all offsets and the final image with a fresh page.
        reveal_dir = tmp / ("reveal-" + name)
        reveal = run(reveal_dir, page, actions + [("scroll", "0,0")], True, reuse_lines)
        for key in ("viewport", "untouched"):
            assert offsets(reveal[-1], key) == offsets(steps[-1], key), (offscreen, key)
        reference = tmp / ("fresh-" + name)
        run(reference, page, [("scroll-element", "untouched,75,60")], False)
        assert (reference / "page.png").read_bytes() == (reveal_dir / "page.png").read_bytes(), offscreen

print("Retained scroll offsets survive, clamp on shrink and update while off-screen.")
