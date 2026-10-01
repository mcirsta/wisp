#!/usr/bin/env python3
"""Compare native state refreshes with the forced-full reference in one build."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/restyle-tracking.html"
ids = ("outside", "field-message", "notes", "notes-message", "tabs", "first", "second",
       "first-panel", "second-panel", "after")
actions = (("click-id", "outside"), ("type", "x"), ("click-id", "notes"), ("type", "y"),
           ("click-label", "second"), ("click-label", "first"), ("click-label", "second"))
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


def run(destination, force, restart=False):
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "600x500",
            "--capture-settle", "50", "--capture-timeout", "20000", "--capture-css-only-restyle"]
    if force:
        args.append("--capture-force-full-restyle")
    if restart:
        args.append("--capture-restart-partial-restyle")
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
        partial = not force and not restart and index > 5
        assert profile["update_mode"] == ("partial_css" if partial else "full"), (force, restart, index, profile)
        reason = ("forced_full" if force else
                  "dom_changed" if index == 5 else "inputs_changed_during_build" if restart else "none")
        assert profile["fallback_reason"] == reason, (force, restart, index, profile)
        assert profile["invalidated_nodes"] > 0 and profile["styled_elements"] > 0, (force, index, profile)
        assert profile["layout_reset_ms"] == 0, (force, index, profile)
        if profile["layout_cpu_ms"] is not None:
            assert 0 < profile["layout_cpu_ms"] <= profile["layout_ms"] + 0.5, (force, index, profile)
        if not force and index > 5:
            assert profile["reused_styles"] > 0 and profile["temporary_bytes"] > 0, (index, profile)
            assert profile["cloned_result_bytes"] > 0 and profile["style_references"] > 0, (index, profile)
            assert profile["restarts"] == int(restart), (index, profile)
        else:
            assert profile["reused_styles"] == 0 and profile["temporary_bytes"] == 0, (force, index, profile)
        assert profile["changed_controls"] == 2 and profile["dirty_roots"] == 1, (force, index, profile)
        elements = {e["id"]: e for e in step["elements"]}
        for element in elements.values():
            for box in element["boxes"]:
                assert box["owns_region"] == (partial and element["id"] == "tabs"), (force, restart, index, element)
                if box["owns_region"]:
                    assert box["box_boundary"] == "ok", (force, restart, index, element)
        for key, height in (("field-message", 17), ("notes-message", 19)):
            boxes = elements[key]["boxes"]
            assert boxes and boxes[0]["content"]["height"] == height, (force, index, key, elements[key])
        selected = "first" if index == 6 else "second"
        other = "second" if selected == "first" else "first"
        assert elements[selected]["state"]["checked"] and not elements[other]["state"]["checked"], (force, index)
        assert elements[selected + "-panel"]["boxes"] and not elements[other + "-panel"]["boxes"], (force, index)
    # Ownership intentionally differs; compare the complete rendered state below.
    for step in steps:
        for element in step["elements"]:
            for box in element["boxes"]:
                del box["owns_region"]
                box.pop("box_boundary", None)
    return steps


with tempfile.TemporaryDirectory(prefix="wisp-restyle-reference-") as tmp:
    full = Path(tmp) / "full"
    full_steps = run(full, True)
    for name, restart in (("normal", False), ("restart", True)):
        normal = Path(tmp) / name
        normal_steps = run(normal, False, restart)
        for index, (a, b) in enumerate(zip(normal_steps, full_steps)):
            for key in ("elements", "scroll", "resource_count", "active_resources", "status"):
                assert a[key] == b[key], f"{name}: state {index}: {key} differs"
        for artifact in ("page.png", "dom.txt"):
            assert (normal / artifact).read_bytes() == (full / artifact).read_bytes(), f"{name}: final {artifact} differs"
print("Selective/full/restarted refreshes match state, geometry, pixels and resources.")
