#!/usr/bin/env python3
"""Exercise native clicks, state-dependent CSS and box reconstruction without JS."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/checked-interaction.html"
ids = ("first", "second", "first-panel", "second-panel", "first-inner", "second-inner",
       "after", "draft", "notes", "box", "extra", "disabled", "implicit", "floating", "frame-toggle", "new-frame-panel", "overlap-first", "overlap-second")
actions = [("click-id", "draft"), ("type", "typed-"), ("click-id", "notes"), ("type", "written-"),
           ("scroll", "0,80"), ("click-id", "second-text"), ("click-label", "first")]
actions += [("click-label", target) for target in ("second", "first") * 6]
actions += [("click-label", "box"), ("click-id", "box"), ("click-label", "disabled"),
            ("click-id", "disabled"), ("click-id", "implicit-text"), ("click-id", "implicit"),
            ("click-id", "floating-label"), ("click-id", "floating-label"),
            ("click-id", "frame-toggle-label"), ("click-id", "frame-toggle-label"),
            ("click-id", "frame-toggle-label"), ("click-id", "overlap-label")]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-checked-interaction-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "600x700",
            "--capture-settle", "50", "--capture-timeout", "20000"]
    for element_id in ids:
        args.extend(("--capture-id", element_id))
    for kind, value in actions:
        args.extend(("--capture-" + kind, value))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0, process.stderr
    assert "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((destination / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, len(steps)
    selected, checkbox, implicit, floating, frame = "first", False, False, False, False
    warmed_resources = None
    for index, step in enumerate(steps):
        if index:
            kind, value = actions[index - 1]
            if value in ("second-text", "first", "second"):
                selected = "second" if value == "second-text" else value
            elif value == "box":
                checkbox = not checkbox
            elif value in ("implicit-text", "implicit"):
                implicit = not implicit
            elif value == "floating-label":
                floating = not floating
            elif value == "frame-toggle-label":
                frame = not frame
        elements = {e["id"]: e for e in step["elements"]}
        def checked(key):
            return elements[key]["state"]["checked"]
        def box(key):
            boxes = elements[key]["boxes"]
            assert boxes, f"Step {index}: missing box {key}"
            return boxes[0]["content"]
        assert checked("first") == (selected == "first"), index
        assert checked("second") == (selected == "second"), index
        other = "second" if selected == "first" else "first"
        assert not elements[other + "-panel"]["boxes"], (index, other)
        expected_height = 100 if selected == "first" else 60
        assert box(selected + "-panel")["height"] == expected_height, index
        # Inherited custom properties must be reselected along with display.
        assert box(selected + "-inner")["height"] == expected_height, index
        assert box("after")["y"] == 140 + expected_height, index
        assert checked("box") == checkbox and bool(elements["extra"]["boxes"]) == checkbox, index
        assert not checked("disabled"), index
        assert checked("implicit") == implicit, index
        assert checked("floating") == floating, index
        assert checked("frame-toggle") == frame and bool(elements["new-frame-panel"]["boxes"]) == frame, index
        assert checked("overlap-first") == (index == len(actions)), index
        assert checked("overlap-second") == (index < len(actions)), index
        if index >= 2:
            assert "typed-" in elements["draft"]["state"]["value"], index
        if index >= 4:
            assert "written-" in elements["notes"]["state"]["value"], index
        if index >= 5:
            assert step["scroll"] == {"x": 0, "y": 80}, (index, step["scroll"])
        assert step["active_resources"] == 0, index
        if index == 6:
            warmed_resources = step["resource_count"]
        elif index > 6:
            assert step["resource_count"] == warmed_resources, (index, step["resource_count"], warmed_resources)
print(f"Native checked interaction: {len(steps)} states; geometry, CSS variables, text, scrolling and resources preserved.")
