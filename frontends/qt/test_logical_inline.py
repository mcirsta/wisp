#!/usr/bin/env python3
"""CSS Logical inline mapping, composition, painting and direction restyling."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

CASES = ("ltr", "rtl", "variable", "cascade", "inherit", "relative", "flex", "pseudo", "dynamic")
WIDTH, HEIGHT = 520, 940


def capture(binary, destination, clicks, full=False):
    fixture = Path(__file__).resolve().parents[2] / "test/logical-inline.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    args = [binary, "--capture-dir", str(destination), "--capture-size", f"{WIDTH}x{HEIGHT}",
            "--capture-settle", "50", "--capture-timeout", "8000", "--capture-css-only-restyle"]
    if full:
        args.append("--capture-force-full-restyle")
    for name in CASES:
        for suffix in ("", "-ref"):
            args.extend(("--capture-id", name + suffix))
    args.extend(("--capture-id", "toggle"))
    for _ in range(clicks):
        args.extend(("--capture-click-label", "toggle"))
    process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
    assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    elements = {item["id"]: item for item in report["elements"]}
    assert elements["toggle"]["state"]["checked"] == bool(clicks % 2), elements["toggle"]
    for name in CASES:
        a, b = elements[name], elements[name + "-ref"]
        assert a["status"] == b["status"] == "ok" and a["boxes"] and b["boxes"], (name, a, b)
        a, b = a["boxes"][0], b["boxes"][0]
        x, y = a["border_box"], b["border_box"]
        assert (x["x"] + 260, x["y"], x["width"], x["height"]) == (
            y["x"], y["y"], y["width"], y["height"]), (name, x, y)
        for key in ("padding_trbl", "margin_trbl"):
            assert a[key] == b[key], (name, key, a[key], b[key])
    # Independently check the two-edge equation against a 220px containing block.
    for name, rtl in (("ltr", False), ("rtl", True), ("dynamic", bool(clicks % 2))):
        box = elements[name]["boxes"][0]
        assert (box["border_box"]["x"], box["border_box"]["width"]) == (26 if rtl else 16, 178), (name, box)
        assert box["padding_trbl"] == ([0, 7, 0, 13] if rtl else [0, 13, 0, 7]), (name, box)
    pixels = subprocess.run([sys.argv[2], str(destination / "page.png"), "-depth", "8", "RGB:-"],
                            capture_output=True, check=True, timeout=10).stdout
    assert len(pixels) == WIDTH * HEIGHT * 3
    for row, name in enumerate(CASES):
        for y in range(30 + row * 100, 130 + row * 100):
            offset = y * WIDTH * 3
            assert pixels[offset:offset + 240 * 3] == pixels[offset + 260 * 3:offset + 500 * 3], (
                name, clicks, full, f"pixel mismatch at y={y}")
    if clicks:
        steps = json.loads((destination / "steps.json").read_text())
        assert len(steps) == clicks + 1, len(steps)
    return pixels


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="wisp-logical-inline-") as tmp:
        for clicks in range(3):
            partial = capture(binary, Path(tmp) / f"partial-{clicks}", clicks)
            if clicks:
                full = capture(binary, Path(tmp) / f"full-{clicks}", clicks, True)
                assert partial == full, f"Partial/full direction refresh differs after {clicks} clicks"
    print(f"Logical inline: {len(CASES)} physical references, geometry/pixels and LTR/RTL/LTR refresh passed.")


if __name__ == "__main__":
    main()
