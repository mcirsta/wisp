#!/usr/bin/env python3
"""Positioned auto/zero stacking: pixels and native label activation agree."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


# Each case overlaps a green label and a red block. The label's centre is the
# sample/click point. Expected winners are independently checked in Chromium.
CASES = [
    ("absolute", "{label}{cover}", "", True),
    ("text", "{label}{cover}", ".label{font:12px/12px sans-serif}", True),
    ("single-paint", '<div><div><div>{label}</div></div></div>{cover}',
     ".label{z-index:1;background:rgba(34,136,68,.5)}", True),
    ("relative", "{label}{cover}", ".label{position:relative}.cover{margin-top:-40px}", True),
    ("zero", "{label}{cover}", ".label{z-index:0}", True),
    ("positive", "{label}{cover}", ".label{z-index:1}", True),
    ("negative", "{label}{cover}", ".label{z-index:-1}", False),
    ("reverse", "{cover}{label}", "", True),
    ("auto-later", "{label}{cover}", ".cover{position:relative}", False),
    ("auto-earlier", "{cover}{label}", ".cover{position:relative}", True),
    ("zero-later", "{label}{cover}", ".cover{position:relative;z-index:0}", False),
    ("zero-earlier", "{cover}{label}", ".cover{position:relative;z-index:0}", True),
    ("nested-auto", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:absolute;left:10px;top:10px}.label{position:static}", True),
    ("escape-positive", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:absolute}.label{z-index:2}.cover{position:relative;z-index:1}", True),
    ("isolate-zero", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:absolute;z-index:0}.label{z-index:2}.cover{position:relative;z-index:1}", False),
    ("escape-negative", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:absolute}.label{z-index:-1}", False),
    ("context-background", '<div class="wrap">{label}</div>',
     ".wrap{position:relative;z-index:0;width:130px;height:70px;background:#c22}.label{z-index:-1}", True),
    ("float-in-auto", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:absolute;left:10px;top:10px}.label{position:static;float:left}", True),
    ("auto-in-float", '<div class="wrap">{label}</div>{cover}',
     ".wrap{float:left;width:90px;height:50px}.label{position:relative}", True),
    ("float-before-auto", '<div class="wrap">{label}</div>{cover}',
     ".wrap{float:left;width:90px;height:50px}.label{position:relative}.cover{position:relative}", False),
    ("float-after-auto", '{cover}<div class="wrap">{label}</div>',
     ".wrap{float:left;width:90px;height:50px;margin-top:-70px}.label{position:relative}.cover{position:relative}", True),
    ("clipped", '<div class="wrap">{label}</div>{cover}',
     ".wrap{overflow:hidden;width:30px;height:60px}.label{position:relative}.cover{margin-top:-60px}", False),
    ("absolute-clipped", '<div class="wrap">{label}</div>{cover}',
     ".wrap{position:relative;overflow:hidden;width:30px;height:60px}.cover{margin-top:-60px}", False),
    ("fixed", "{label}{cover}", ".case{transform:translate(0,0)}.label{position:fixed}", True),
    ("sticky", "{label}{cover}", ".label{position:sticky}.cover{margin-top:-40px}", True),
]


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><style>',
             'html,body{margin:0;font-size:0}input{display:none}',
             '.case{position:relative;z-index:0;display:inline-block;vertical-align:top;'
             'width:150px;height:90px;margin:5px;background:white}',
             '.label{position:absolute;display:block;left:10px;top:10px;width:80px;height:40px;background:#284}',
             '.cover{width:130px;height:70px;background:#c22}']
    for name, _, style, _ in CASES:
        for rule in style.split('}'):
            if '{' in rule:
                selector, body = rule.split('{')
                selector = f'#{name}' if selector == '.case' else f'#{name} {selector}'
                parts.append(f'{selector}{{{body}}}')
    parts.append('</style>')
    for name, markup, _, _ in CASES:
        text = '<span>Click</span>' if name == 'text' else ''
        label = f'<label class="label" id="{name}-label" for="{name}-input">{text}</label>'
        cover = '<div class="cover"></div>'
        parts.append(f'<input type="checkbox" id="{name}-input"><div class="case" id="{name}">'
                     + markup.format(label=label, cover=cover) + '</div>')
    return '\n'.join(parts)


def main(zoom):
    binary = Path(sys.argv[1]).resolve()
    magick = shutil.which('magick')
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    width, height = int(800 * zoom), int(600 * zoom)
    with tempfile.TemporaryDirectory(prefix="wisp-positioned-stacking-") as tmp:
        fixture = Path(tmp) / 'fixture.html'
        fixture.write_text(fixture_html())
        dest = Path(tmp) / 'capture'
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', f'{width}x{height}',
                '--capture-settle', '50', '--capture-timeout', '12000', f'--scale={int(100 * zoom)}']
        for name, _, _, _ in CASES:
            args += ['--capture-id', name + '-input', '--capture-id', name + '-label']
            args += ['--capture-click-id', name + '-label']
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        assert 'runtime error:' not in result.stderr, result.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == len(CASES) + 1, len(steps)
        failures = []
        for index, step in enumerate(steps):
            elements = {e['id']: e for e in step['elements']}
            for case_index, (name, _, _, winner) in enumerate(CASES):
                expected = winner and index > case_index
                actual = elements[name + '-input']['state']['checked']
                if actual != expected:
                    failures.append(f'step {index}, {name}: checked {actual}, expected {expected}')
        if magick:
            pixels = subprocess.run([magick, str(dest / 'page.png'), '-depth', '8', 'RGB:-'],
                                    check=True, capture_output=True).stdout
            assert len(pixels) == width * height * 3
            for name, _, _, winner in CASES:
                r = elements[name + '-label']['boxes'][0]['border_box']
                x = int((r['x'] + r['width'] / 2) * zoom)
                y = int((r['y'] + r['height'] / 2) * zoom)
                assert 0 <= x < width and 0 <= y < height, (name, x, y)
                offset = (y * width + x) * 3
                actual = tuple(pixels[offset:offset + 3])
                expected = (34, 136, 68) if winner else (204, 34, 34)
                if name == 'single-paint':
                    expected = (119, 85, 51)  # One translucent green layer over red.
                if actual != expected:
                    failures.append(f'{name}: pixel {actual}, expected {expected}')
        assert not failures, '\n'.join(failures)
    print(f'Positioned stacking: {len(CASES)} native click cases passed at {zoom * 100:g}%; '
          f"pixels {'passed' if magick else 'not checked (ImageMagick unavailable)'}.")


if __name__ == '__main__':
    for zoom in (1, 1.5):
        main(zoom)
