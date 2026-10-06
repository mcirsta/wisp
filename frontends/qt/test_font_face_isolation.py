#!/usr/bin/env python3
"""Exercise CSS parsing, downloads, face selection and final HTML measurement."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/font-face-isolation.html"
# Generated test faces: advances in 1000 units/em, four glyphs at 20px.
expected = {"reading": 48, "heavy": 60, "a": 40, "b": 72, "regular": 68, "bold": 44}
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-font-face-") as tmp:
    destination = Path(tmp)
    args = [str(binary), "--capture-dir", tmp, "--capture-size", "400x420",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for name in expected:
        args += ["--capture-id", name]
    result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    elements = json.loads((destination / "inspection.json").read_text())["elements"]
    for element in elements:
        assert element["status"] == "ok" and element.get("boxes"), element
        width = element["boxes"][0]["content"]["width"]
        assert abs(width - expected[element["id"]]) <= 1, (element["id"], width, expected[element["id"]])
print("CSS font-face isolation: family identity, descriptors, compressed resources and layout passed.")
