#!/usr/bin/env python3
"""Hints paint like control values, including CSS styles and UTF-8 multiline text."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    magick = sys.argv[2]
    fixture = Path(__file__).resolve().parents[2] / "test/input-placeholders.html"
    cases = ("default", "styled", "legacy", "password", "filled", "readonly", "newlines", "multiline", "empty",
             "block", "flex", "grid", "stretched")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-placeholders-") as tmp:
        destination = Path(tmp)
        args = [str(binary), "--capture-dir", str(destination), "--capture-size", "540x900",
                "--capture-settle", "100", "--capture-timeout", "8000"]
        for name in cases:
            args += ["--capture-id", name, "--capture-id", name + "-reference"]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == 0, process.stderr
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        elements = json.loads((destination / "inspection.json").read_text())["elements"]
        assert all(e["status"] == "ok" and e.get("boxes") for e in elements), elements
        boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}

        def pixels(box):
            # Exclude the border: this checks painted text, not just geometry.
            geometry = f'{box["width"] - 2}x{box["height"] - 2}+{box["x"] + 1}+{box["y"] + 1}'
            return subprocess.run([magick, str(destination / "page.png"), "-crop", geometry,
                                   "+repage", "-depth", "8", "RGB:-"],
                                  check=True, capture_output=True, timeout=5).stdout

        for name in cases:
            hint, reference = boxes[name], boxes[name + "-reference"]
            assert (hint["width"], hint["height"]) == (reference["width"], reference["height"]), name
            actual, expected = pixels(hint), pixels(reference)
            assert actual == expected, f"{name}: hint pixels differ from equivalent value"
            if name != "empty":
                assert min(actual) < 200, f"{name}: both controls unexpectedly blank"
    print("Placeholders: default/CSS/legacy styles, passwords, values, readonly, newlines and UTF-8 wrapping passed.")


if __name__ == "__main__":
    main()
