#!/usr/bin/env python3
"""HTML object fallback for unusable ICO/BMP data; usable partial images survive.

https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
Alternate-member selection and partial-pixel recovery are decoder policies.
"""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def dib(width=16, bpp=32, rows=16, masks=0, alpha=255):
    height = 16
    palette = bytes((0, 0, 255, 0, 0, 255, 0, 0)) if bpp < 16 else b''
    header = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, bpp, 0, 0, 0, 0,
                         2 if palette else 0, 0)
    if bpp == 32:
        row = bytes((0, 0, 255, alpha)) * width
    elif bpp == 24:
        row = bytes((0, 0, 255)) * width
    elif bpp == 16:
        row = b'\x00\x7c' * width
    else:
        row = bytes((width * bpp + 7) // 8)
    row += bytes((-len(row)) % 4)
    return header + palette + row * rows + bytes(((width + 31) // 32) * 4 * masks)


def ico(*images):
    directory = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for image in images:
        width = struct.unpack_from('<i', image, 4)[0] if len(image) >= 16 else 16
        bpp = struct.unpack_from('<H', image, 14)[0] if len(image) >= 16 else 32
        directory += struct.pack('<BBBBHHII', width % 256, 16, 0, 0, 1, bpp, len(image), offset)
        offset += len(image)
    return directory + b''.join(images)


def bmp(rows):
    image = bytearray(dib(rows=rows))
    struct.pack_into('<i', image, 8, 16)
    # One byte of incomplete pixels keeps the BMP file offset within its data.
    if rows == 0:
        image += b'\0'
    return b'BM' + struct.pack('<IHHI', 14 + len(image), 0, 0, 54) + image


def uri(data, mime='image/x-icon'):
    return 'data:' + mime + ';base64,' + base64.b64encode(data).decode()


# Fourteen pixel bytes pass the old header checks but cannot fill a single row.
bad = dib(rows=0) + bytes(14)
cases = {
    'broken-0': uri(ico(bad)),
    'broken-1': uri(ico(bad)),
    'all-broken': uri(ico(bad, dib(width=32, rows=0) + bytes(14))),
    'alternate': uri(ico(dib(width=32), bad)),
    'bad-header': uri(ico(dib(), b'not a supported DIB header')),
    'missing-mask': uri(ico(dib(bpp=24))),
    'transparent': uri(ico(dib(alpha=0))),
    'complete': uri(ico(dib(bpp=24, masks=16))),
    'bmp-broken': uri(bmp(0), 'image/bmp'),
    'bmp-partial': uri(bmp(8), 'image/bmp'),
}
for depth in (1, 4, 8, 16, 24, 32):
    cases[f'partial-{depth}'] = uri(ico(dib(bpp=depth, rows=8)))
failed = {'broken-0', 'broken-1', 'all-broken', 'bmp-broken'}
page = ('<!doctype html><style>body{margin:0;background:white}.case{display:inline-block;'
        'width:100px;height:80px;vertical-align:top;font:10px sans-serif}'
        'object{display:block;width:32px;height:32px}p{margin:0}</style>'
        '<details open id="panel"><summary id="summary">Icons</summary>' +
        ''.join(f'<div class="case">{name}<object id="{name}" data="{source}">'
                f'<p id="fallback-{name}">ico-fallback</p></object></div>'
                for name, source in cases.items()) + '</details>')
binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
with tempfile.TemporaryDirectory(prefix='wisp-ico-fallback-') as tmp:
    root = Path(tmp)
    fixture = root / 'page.html'
    fixture.write_text(page)
    out = root / 'capture'
    args = [str(binary), '--capture-dir', str(out), '--capture-size', '900x600',
            '--capture-settle', '100', '--capture-timeout', '15000', '--enable_javascript=0',
            '--capture-id', 'panel']
    for name in cases:
        args += ['--capture-id', name, '--capture-id', 'fallback-' + name]
    args += ['--capture-click-id', 'summary', '--capture-find', 'ico-fallback',
             '--capture-resize', '800x600', '--capture-click-id', 'summary', '--capture-click-id', 'summary']
    result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
    assert 'UNDERFLOW' not in result.stderr, result.stderr
    meta = json.loads((out / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], meta
    steps = json.loads((out / 'steps.json').read_text())
    assert len(steps) == 6, len(steps)
    for index, step in enumerate(steps):
        elements = {e['id']: e for e in step['elements']}
        opened = index not in (1, 4)
        assert elements['panel']['state']['open'] == opened, index
        for name in cases:
            expected = opened and name in failed
            assert bool(elements['fallback-' + name]['boxes']) == expected, (index, name)
        assert step['active_resources'] == 0, (index, step['active_resources'])
        assert step['resource_count'] == steps[0]['resource_count'] == len(cases), index
        if index == 2:
            assert elements['fallback-broken-0']['boxes'][0]['highlighted_fragments'] == 1
    pixels = subprocess.run([magick, str(out / 'page.png'), '-depth', '8', 'RGB:-'],
                            check=True, capture_output=True, timeout=5).stdout
    assert len(pixels) == 800 * 600 * 3
    for name in cases.keys() - failed:
        box = elements[name]['boxes'][0]['border_box']
        for fraction in (.15, .85):
            if name == 'transparent' or name.startswith('partial-') and fraction < .5:
                expected = (255, 255, 255)
            elif name == 'bmp-partial' and fraction < .5:
                expected = (0, 0, 0)  # BMP's opaque canvas is cleared to black.
            else:
                expected = (248 if name == 'partial-16' else 255, 0, 0)
            x = int(box['x'] + box['width'] / 2)
            y = int(box['y'] + box['height'] * fraction)
            offset = (y * 800 + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            assert actual == expected, (name, fraction, expected, actual)
print('ICO/BMP fallback, alternate entries, partial colours/alpha, masks and repeated reveal passed.')
