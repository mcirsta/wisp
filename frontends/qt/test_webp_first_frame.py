#!/usr/bin/env python3
"""Animated WebP uses its full first canvas, including offsets and alpha."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 320, 60
BASE, RED, BLENDED = (32, 64, 96), (255, 0, 0), (136, 72, 58)
SAMPLES = [
    ("static", 20, 20, RED),
    ("opaque first frame", 70, 20, RED),
    ("alpha outside frame", 113, 13, BASE),
    ("alpha inside frame", 125, 22, BLENDED),
    ("alpha below frame", 125, 31, BASE),
    ("opaque outside frame", 163, 13, BASE),
    ("opaque inside frame", 175, 22, RED),
    ("opaque below frame", 175, 31, BASE),
    ("background outside frame", 213, 13, BASE),
    ("background inside frame", 225, 22, BLENDED),
    ("static alpha", 268, 16, BLENDED),
]


def main():
    binary = Path(sys.argv[1]).resolve()
    magick = sys.argv[2]
    fixture = Path(__file__).resolve().parents[2] / "test/webp-first-frame.html"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-webp-") as tmp:
        # Even with animation enabled, WebP currently stays on frame one.
        for animate, settle in ((0, 50), (1, 600)):
            destination = Path(tmp) / str(animate)
            args = [str(binary), "--capture-dir", str(destination),
                    "--capture-size", f"{WIDTH}x{HEIGHT}", "--capture-settle", str(settle),
                    "--capture-timeout", "8000", f"--animate_images={animate}", str(fixture)]
            process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
            assert process.returncode == 0, process.stderr
            meta = json.loads((destination / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], meta
            assert meta["animate_images"] == bool(animate), meta
            pixels = subprocess.run([magick, str(destination / "page.png"), "-depth", "8", "RGB:-"],
                                    check=True, capture_output=True, timeout=5).stdout
            assert len(pixels) == WIDTH * HEIGHT * 3
            failures = []
            for name, x, y, expected in SAMPLES:
                offset = (y * WIDTH + x) * 3
                actual = tuple(pixels[offset:offset + 3])
                if any(abs(a - b) > 2 for a, b in zip(actual, expected)):
                    failures.append(f"{name} (animate={animate}) at {x},{y}: expected {expected}, got {actual}")
            assert not failures, "\n".join(failures)
    print("WebP: static images, composited first frames, alpha, offsets and disabled playback passed.")


if __name__ == "__main__":
    main()
