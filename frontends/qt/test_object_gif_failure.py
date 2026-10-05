#!/usr/bin/env python3
"""Unrenderable GIF objects fall back; usable partial/earlier frames stay images.

HTML object image-handler rule:
https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
Recovery to an earlier frame is our decoder policy, not a required animation rule.
"""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def gif(codes, width=1):
    # Red/green global palette. Each frame supplies just one pixel; the two
    # pixel fixture intentionally ends early and remains partially usable.
    data = b'GIF89a' + struct.pack('<HHBBB', width, 1, 0x80, 0, 0) + bytes((255, 0, 0, 0, 255, 0))
    for index, code in enumerate(codes):
        # Dispose to transparent background, so a failed frame also changes
        # the compositing bitmap and recovery must actually rebuild it.
        data += bytes((0x21, 0xf9, 4, 9 if index == 0 else 8, 2, 0, 1, 0))
        data += b',' + struct.pack('<HHHHB', 0, 0, width, 1, 0) + bytes((2, 1, code, 0))
    return 'data:image/gif;base64,' + base64.b64encode(data + b';').decode()


sources = {
    'broken-0': gif([0x3c]),  # Clear code followed by invalid dictionary index 7.
    'broken-1': gif([0x3c]),
    'partial': gif([0x44], width=2),
    'transparent': gif([0x4c]),
    'damaged-animation': gif([0x44, 0x3c]),
    'valid-animation': gif([0x44, 0x4c]),
}
page = ('<!doctype html><style>body{background:white}object{display:block;width:40px;height:20px;'
        'margin:4px}p{margin:0}</style><details open id="panel"><summary id="summary">GIFs</summary>' +
        ''.join(f'<object id="{name}" data="{source}"><p id="fallback-{name}">gif-fallback</p></object>'
                for name, source in sources.items()) + '</details>')
binary = Path(sys.argv[1]).resolve()
magick = sys.argv[2]
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
with tempfile.TemporaryDirectory(prefix='wisp-gif-fallback-') as tmp:
    root = Path(tmp)
    fixture = root / 'page.html'
    fixture.write_text(page)
    for animate in (0, 1):
        out = root / str(animate)
        args = [str(binary), '--capture-dir', str(out), '--capture-size', '500x400',
                '--capture-settle', '400', '--capture-timeout', '15000', '--enable_javascript=0',
                f'--animate_images={animate}', '--capture-id', 'panel']
        for name in sources:
            args += ['--capture-id', name, '--capture-id', 'fallback-' + name]
        args += ['--capture-click-id', 'summary', '--capture-find', 'gif-fallback',
                 '--capture-click-id', 'summary', '--capture-click-id', 'summary']
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
        assert 'UNDERFLOW' not in result.stderr, result.stderr
        meta = json.loads((out / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((out / 'steps.json').read_text())
        assert len(steps) == 5, len(steps)
        for index, step in enumerate(steps):
            elements = {e['id']: e for e in step['elements']}
            opened = index not in (1, 3)
            assert elements['panel']['state']['open'] == opened, (animate, index)
            for name in sources:
                expected = opened and name.startswith('broken')
                assert bool(elements['fallback-' + name]['boxes']) == expected, (animate, index, name)
            assert step['active_resources'] == 0, (animate, index, step['active_resources'])
            assert step['resource_count'] == steps[0]['resource_count'] == len(sources), (animate, index)
            if index == 2:
                assert elements['fallback-broken-0']['boxes'][0]['highlighted_fragments'] == 1
        pixels = subprocess.run([magick, str(out / 'page.png'), '-depth', '8', 'RGB:-'],
                                check=True, capture_output=True, timeout=5).stdout
        assert len(pixels) == 500 * 400 * 3
        for name, fraction, expected in (
                ('partial', .2, (255, 0, 0)), ('partial', .8, (255, 255, 255)),
                ('transparent', .5, (255, 255, 255)),
                ('damaged-animation', .5, (255, 0, 0)),
                ('valid-animation', .5, (0, 255, 0) if animate else (255, 0, 0))):
            box = elements[name]['boxes'][0]['border_box']
            x = int(box['x'] + box['width'] * fraction)
            y = int(box['y'] + box['height'] / 2)
            offset = (y * 500 + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            assert actual == expected, (animate, name, fraction, expected, actual)
print('GIF object fallback, partial pixels, animation recovery and repeated reveal passed.')
