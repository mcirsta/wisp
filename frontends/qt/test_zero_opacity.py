#!/usr/bin/env python3
"""Zero opacity suppresses paint, including flattened/deferred boxes, but keeps layout and hits."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
fixture = Path(__file__).resolve().parents[2] / "test/zero-opacity.html"
cases = ("block", "inline", "wrap", "split", "float", "stack", "pseudo", "pre", "list",
         "flex", "grid", "table", "control", "visibility", "hit-target")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")

with tempfile.TemporaryDirectory(prefix="wisp-zero-opacity-") as tmp:
    root = Path(tmp)
    reference = root / "reference.html"
    # Force every affected descendant invisible independently. This preserves
    # layout and provides a paint reference without relying on opacity itself.
    reference.write_text(fixture.read_text().replace("/* REFERENCE OVERRIDES */", """
        .hidden, .hidden *, .generated::before, .own-before::before, .own-after::after {
            visibility: hidden !important;
        }
    """))
    for zoom in (1, 1.5):
        captures = []
        for name, page in (("actual", fixture), ("reference", reference)):
            destination = root / f"{name}-{zoom}"
            args = [str(binary), "--capture-dir", str(destination),
                    "--capture-size", f"{int(420 * zoom)}x{int(1300 * zoom)}",
                    "--capture-settle", "100", "--capture-timeout", "8000", f"--scale={int(100 * zoom)}",
                    "--capture-point", "15,15"]
            for eid in cases:
                args += ["--capture-id", eid]
            process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=15)
            assert process.returncode == 0, (name, zoom, process.stderr)
            meta = json.loads((destination / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (name, zoom, meta)
            report = json.loads((destination / "inspection.json").read_text())
            elements = report["elements"]
            assert all(e["status"] == "ok" and e.get("boxes") for e in elements), (name, elements)
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}
            pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                    check=True, capture_output=True, timeout=5).stdout
            captures.append((boxes, pixels, meta))
            if name == "actual":
                assert any(h["node"]["id"] == "hit-target" for h in report["point"]["hits"]), report["point"]
        actual, expected = captures
        assert actual[0] == expected[0], (zoom, actual[0], expected[0])
        assert actual[2]["document_extent"] == expected[2]["document_extent"], zoom
        assert actual[1] == expected[1], f"zoom {zoom}: transparent subtree leaked pixels"
        assert actual[1].count(bytes((22, 136, 68))) > 1000, "visible markers disappeared"

print("Zero opacity: layout, hit testing, inline fragments, pseudo-elements, floats, z-index and zoom passed.")
