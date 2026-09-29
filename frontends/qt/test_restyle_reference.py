#!/usr/bin/env python3
"""Compare native state refreshes with the forced-full reference in one build."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
tracking = sys.argv[2] == "1"
fixture = Path(__file__).resolve().parents[2] / "test/restyle-tracking.html"
ids = ("outside", "field-message", "notes", "notes-message", "tabs", "first", "second",
       "first-panel", "second-panel", "after")
actions = (("click-id", "outside"), ("type", "x"), ("click-id", "notes"), ("type", "y"),
           ("click-label", "second"), ("click-label", "first"), ("click-label", "second"))
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def run(destination, force):
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "600x500",
            "--capture-settle", "50", "--capture-timeout", "20000"]
    if force:
        args.append("--capture-force-full-restyle")
    for key in ids:
        args.extend(("--capture-id", key))
    for kind, value in actions:
        args.extend(("--capture-" + kind, value))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0, (force, process.stderr)
    assert "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], (force, meta)
    steps = json.loads((destination / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, (force, len(steps))
    for index in (5, 6, 7):
        step = steps[index]
        profile = step["restyle_profile"]
        # Tracking is preparatory: no selective rendering is implemented yet.
        assert profile["update_mode"] == "full", (force, index, profile)
        reason = ("forced_full" if force else "dom_changed" if index == 5 else "partial_not_implemented")
        assert profile["fallback_reason"] == (reason if tracking else "feature_disabled"), (force, index, profile)
        assert profile["invalidated_nodes"] > 0 and profile["styled_elements"] > 0, (force, index, profile)
        assert profile["reused_styles"] == 0 and profile["temporary_bytes"] == 0, (force, index, profile)
        if tracking:
            assert profile["changed_controls"] == 2 and profile["dirty_roots"] == 1, (force, index, profile)
        elements = {e["id"]: e for e in step["elements"]}
        for key, height in (("field-message", 17), ("notes-message", 19)):
            boxes = elements[key]["boxes"]
            assert boxes and boxes[0]["content"]["height"] == height, (force, index, key, elements[key])
        selected = "first" if index == 6 else "second"
        other = "second" if selected == "first" else "first"
        assert elements[selected]["state"]["checked"] and not elements[other]["state"]["checked"], (force, index)
        assert elements[selected + "-panel"]["boxes"] and not elements[other + "-panel"]["boxes"], (force, index)
    return steps


with tempfile.TemporaryDirectory(prefix="wisp-restyle-reference-") as tmp:
    normal, full = Path(tmp) / "normal", Path(tmp) / "full"
    normal_steps, full_steps = run(normal, False), run(full, True)
    for index, (a, b) in enumerate(zip(normal_steps, full_steps)):
        for key in ("elements", "scroll", "resource_count", "active_resources", "status"):
            assert a[key] == b[key], f"State {index} ({actions[index-1] if index else 'initial'}): {key} differs"
    for name in ("page.png", "dom.txt"):
        assert (normal / name).read_bytes() == (full / name).read_bytes(), f"Final {name} differs"
print("Full-refresh reference: matching state, geometry, pixels and resources; edit and radio tracking verified.")
