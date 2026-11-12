#!/usr/bin/env python3
"""Native Qt wheel input must preserve distance at normal and high DPI."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
platform = sys.argv[2] if len(sys.argv) > 2 else "offscreen"
with tempfile.TemporaryDirectory(prefix="wisp-wheel-") as tmp:
    root = Path(tmp)
    page = root / "page.html"
    page.write_text('<!doctype html><style>body{margin:0;width:4000px;height:8000px}</style><p>Wheel input</p>')
    for dpr in (1, 1.5, 1.6, 2):
        dest = root / str(dpr)
        actions = [("wheel-angle", "0,-120"), ("wheel-angle", "0,-240")]
        actions += [("wheel-angle", "0,-30")] * 4
        actions += [("wheel-angle", "0,120"), ("scroll", "500,500"),
                    ("wheel-angle", "-120,0"), ("wheel-angle", "120,0"),
                    ("wheel-pixel", "15,-25"), ("scroll", "0,0"),
                    ("wheel-angle", "0,120"), ("wheel-angle", "120,0")]
        actions += [("wheel-angle", "0,-1")] * 4
        actions += [("scroll", "0,500"), ("wheel-angle", "0,-120,0,-10"),
                    ("wheel-pixel", "0,-10,0,-120")]
        actions += [("scroll", "0,0"), ("resize", "800x720"), ("wheel-angle", "0,-120,0,-15")]
        env = dict(os.environ, QT_QPA_PLATFORM=platform, QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                "--capture-settle", "50", "--capture-timeout", "10000"]
        for kind, value in actions:
            args += ["--capture-" + kind, value]
        run = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=15)
        assert run.returncode == 0 and "runtime error:" not in run.stderr, (dpr, run.stderr)
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["load_complete"] and meta["result"] == "complete", meta
        assert meta["device_pixel_ratio"] == dpr, meta
        lines = meta["wheel_scroll_lines"]
        assert lines == 3, ("Test expects Qt's isolated default of three lines per notch", lines)
        steps = json.loads((dest / "steps.json").read_text())
        actual = [(s["scroll"]["x"], s["scroll"]["y"]) for s in steps]
        # A line is 40 logical pixels. Wheel travel scales with notch count,
        # independent of window size/DPR, and retains fine wheel input.
        expected = [(0, 0), (0, 120), (0, 360), (0, 390), (0, 420), (0, 450), (0, 480),
                    (0, 360), (500, 500), (620, 500), (500, 500),
                    (500, 500) if platform == "xcb" else (485, 525),
                    (0, 0), (0, 0), (0, 0), (0, 1), (0, 2), (0, 3), (0, 4)]
        # Capture uses a shared seat labelled TouchPad, as on Qt Wayland.
        # Only gesture events should use the small accompanying pixel delta.
        # X11 always uses the available angle instead.
        expected += [(0, 500), (0, 620), (0, 740 if platform == "xcb" else 630)]
        expected += [(0, 0), (0, 0), (0, 120)]
        assert actual == expected, (platform, dpr, actual, expected)
print("Qt wheel input: notches, fine deltas, pixels, shared-seat type, boundaries and DPR 1/1.5/1.6/2 passed.")
