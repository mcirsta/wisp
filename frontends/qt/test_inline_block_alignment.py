#!/usr/bin/env python3
"""Real DOM/box-tree regressions for inline-block line metrics and alignment."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/inline-block-alignment.html"
ids = [name + suffix for name in ("middle", "top", "bottom", "baseline", "edges", "image", "wrap", "zero")
       for suffix in ("", "-tall", "-short")]
ids += ["nested", "nested-logo", "nested-brand", "last", "last-block", "last-line", "last-reference",
        "clipped", "clipped-reference", "clipped-block", "soft-block", "soft-reference",
        "button-block", "button-reference", "button-clipped-block", "button-clipped-reference"]
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-inline-block-") as tmp:
    destination = Path(tmp) / "capture"
    args = [str(binary), "--capture-dir", str(destination), "--capture-size", "700x1500",
            "--capture-settle", "100", "--capture-timeout", "8000"]
    for eid in ids:
        args.extend(("--capture-id", eid))
    result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    meta = json.loads((destination / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    report = json.loads((destination / "inspection.json").read_text())
    for element in report["elements"]:
        assert element["status"] == "ok" and element.get("boxes"), element
    boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}

    def bottom(name):
        return boxes[name]["y"] + boxes[name]["height"]

    for name, offset in (("middle", 20), ("top", 0), ("bottom", 40), ("baseline", 40),
                         ("image", 20), ("zero", 20)):
        tall, short = boxes[name + "-tall"], boxes[name + "-short"]
        assert short["y"] - tall["y"] == offset, (name, tall, short)
        if name != "baseline":
            assert boxes[name]["height"] == 80, (name, boxes[name])
    assert boxes["baseline"]["height"] > 80, boxes["baseline"]
    assert boxes["nested-brand"]["y"] - boxes["nested-logo"]["y"] == 10, (
        "CNX nested middle", boxes["nested-logo"], boxes["nested-brand"])
    assert boxes["nested"]["height"] == 80, boxes["nested"]
    # Alignment is based on the margin box, including asymmetric edges.
    tall, short = boxes["edges-tall"], boxes["edges-short"]
    assert 2 * tall["y"] - 7 + tall["height"] + 9 == 2 * short["y"] + short["height"], (tall, short)
    assert boxes["last-line"]["y"] == boxes["last-reference"]["y"], (
        "last in-flow line baseline", boxes["last-line"], boxes["last-reference"])
    assert bottom("clipped-block") + 7 == bottom("clipped-reference"), (
        "overflow baseline is bottom margin", boxes["clipped-block"], boxes["clipped-reference"])
    assert boxes["wrap-short"]["x"] == boxes["wrap-tall"]["x"], ("wrap", boxes["wrap-short"])
    assert boxes["wrap-short"]["y"] - boxes["wrap-tall"]["y"] == 80, ("wrap", boxes["wrap-short"])
    assert boxes["wrap"]["height"] == 120, boxes["wrap"]
    assert boxes["soft-reference"]["y"] - boxes["soft-block"]["y"] == 30, (
        "last wrapped line baseline", boxes["soft-block"], boxes["soft-reference"])
    for name in ("button", "button-clipped"):
        assert boxes[name + "-reference"]["y"] - boxes[name + "-block"]["y"] == 3, (
            name, "content baseline", boxes[name + "-block"], boxes[name + "-reference"])

print("Inline-block alignment: middle/top/bottom, last-line baselines, overflow, edges and wrapping passed.")
