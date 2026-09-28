#!/usr/bin/env python3
"""Regenerate tiny lossless WebP fixtures; requires ImageMagick's `magick`.

Build the animation container explicitly so the encoder cannot optimize away
the partial first frame or its offset. Format: https://developers.google.com/
speed/webp/docs/riff_container . Each second frame is blue and lasts 10 seconds;
the first lasts 100 ms. Tests must keep showing the first frame after that time.
"""
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 32, 24


def chunk(kind, data):
    return kind + struct.pack("<I", len(data)) + data + b"\0" * (len(data) % 2)


def uint24(value):
    return value.to_bytes(3, "little")


def still(width, height, color):
    return subprocess.run([
        "magick", "-size", f"{width}x{height}", f"xc:{color}",
        "-define", "webp:lossless=true", "webp:-",
    ], check=True, capture_output=True).stdout


def frame(image, width, height, x, y, duration):
    # All fixture still images have a single VP8L chunk. ANMF embeds that chunk,
    # not its RIFF header. No blending and no disposal (flags = 2).
    assert image[12:16] == b"VP8L"
    header = b"".join(uint24(n) for n in (x // 2, y // 2, width - 1, height - 1, duration))
    return chunk(b"ANMF", header + b"\x02" + image[12:])


def animation(name, first, width, height, x=0, y=0, alpha=False):
    header = bytes([0x12 if alpha else 0x02, 0, 0, 0]) + uint24(WIDTH - 1) + uint24(HEIGHT - 1)
    data = b"WEBP" + chunk(b"VP8X", header) + chunk(b"ANIM", b"\0" * 6)
    data += frame(first, width, height, x, y, 100)
    data += frame(still(WIDTH, HEIGHT, "blue"), WIDTH, HEIGHT, 0, 0, 10000)
    (ROOT / name).write_bytes(chunk(b"RIFF", data))


red = still(WIDTH, HEIGHT, "red")
alpha = still(16, 12, "rgba(240,80,20,0.5)")
(ROOT / "static.webp").write_bytes(red)
(ROOT / "static-alpha.webp").write_bytes(alpha)
animation("opaque.webp", red, WIDTH, HEIGHT)
animation("offset-alpha.webp", alpha, 16, 12, 8, 6, alpha=True)
# No encoded alpha, but the area outside the first frame is transparent.
animation("offset-opaque.webp", still(16, 12, "red"), 16, 12, 8, 6)
