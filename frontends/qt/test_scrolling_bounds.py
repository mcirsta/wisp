#!/usr/bin/env python3
"""Document overflow uses containing-block coordinates exactly once."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(__file__).resolve().parents[2] / "test/scrolling-bounds.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    # The normal-flow content ends at y=1200. Only actual overflow may extend it.
    cases = [
        ("narrow", 390, "", "", (390, 1200)),
        ("wide", 640, "", "", (640, 1200)),
        ("fragment", 390, "", "#nav", (390, 1200)),
        ("positive-overflow", 390,
         "#sprite { left:470px; top:1300px; width:20px; height:20px; }", "", (490, 1320)),
        ("negative-overflow", 390,
         "#sprite { left:-500px; top:-500px; width:20px; height:20px; }", "", (390, 1200)),
        ("unclipped-overflow", 390, "#clip { overflow:visible; }", "", (830, 1200)),
        ("nested-overflow", 390,
         "#next { position:relative; } #label { left:10px; top:10px; width:500px; height:200px; "
         "clip:auto; overflow:visible; }", "", (830, 1210)),
        ("auto-clip-edges", 390,
         "#next { position:relative; } #label { left:10px; top:10px; width:500px; height:200px; "
         "clip:rect(auto,500px,auto,0px); overflow:visible; }", "", (830, 1210)),
    ]
    with tempfile.TemporaryDirectory(prefix="wisp-scrolling-bounds-") as tmp:
        root = Path(tmp)
        for name, width, style, fragment, extent in cases:
            page = root / f"{name}.html"
            page.write_text(fixture.read_text().replace("</style>", style + "\n</style>"))
            destination = root / name
            args = [str(binary), "--capture-dir", str(destination), "--capture-size", f"{width}x900",
                    "--capture-settle", "100", "--capture-timeout", "8000"]
            for eid in ("nav", "next", "label", "sprite", "viewport-marker"):
                args += ["--capture-id", eid]
            process = subprocess.run([*args, page.as_uri() + fragment], env=env,
                                     capture_output=True, text=True, timeout=15)
            assert process.returncode == 0, (name, process.stderr)
            meta = json.loads((destination / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (name, meta)
            actual = meta["document_extent"]
            assert (actual["width"], actual["height"]) == extent, (name, actual, extent)
            elements = json.loads((destination / "inspection.json").read_text())["elements"]
            for element in elements:
                assert element["status"] == "ok" and element.get("boxes"), (name, element)
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in elements}
            assert boxes["next"] == dict(x=width - 70, y=1000, width=40, height=40), (name, boxes["next"])
            if fragment:
                # The fixed marker exposes the actual document scroll offset.
                # Navigation to an inset target must not scroll a fitting page sideways.
                assert boxes["viewport-marker"]["x"] == 0, boxes["viewport-marker"]
                assert boxes["viewport-marker"]["y"] == 300, boxes["viewport-marker"]
    print("Scrolling bounds: empty/hidden absolute boxes, floats, clipping, real overflow and fragments passed.")


if __name__ == "__main__":
    main()
