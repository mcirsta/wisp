#!/usr/bin/env python3
"""CSS 2.2 Appendix E.2: block backgrounds, floats, inline content, positioned content.

Pixels and native label activation must agree. Expectations follow the painting
phases, including atomic floats/inline-blocks and escaping positioned descendants.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


CASES = [
    ('positive', '{float}{label}', '', True),
    ('right', '{float}{label}', '.float{float:right}.label{margin-left:80px;margin-right:-80px}', True),
    ('zero', '{float}{label}', '.float{height:0}.label{margin-left:0}', True),
    ('inline-background', '{float}{label}', '.label{display:inline;padding:10px 40px}', True),
    ('block-background', '{float}{label}', '.label{display:block;margin-left:0;height:40px}', False),
    ('block-before-float', '{label}{float}',
     '.label{display:block;margin-left:0;height:40px}.float{margin-top:-40px}', False),
    ('nested-inline', '{float}<div class="wrap">{label}</div>', '.wrap{background:#26c}', True),
    ('nested-float', '<div class="wrap">{float}</div>{label}', '.wrap{height:0}', True),
    ('later-block', '{label}<div class="cover"></div>',
     '.label{margin-left:0}.cover{margin-top:-20px}', True),
    ('later-nested-block', '<div>{label}</div><div><div class="cover"></div></div>',
     '.label{margin-left:0}.cover{margin-top:-20px}', True),
    ('float-later', '{label}{float}',
     '.label{float:left;margin-left:0;height:40px}.float{margin-left:-80px}', False),
    ('float-earlier', '{float}{label}', '.label{float:left;height:40px}', True),
    ('nested-float-group', '<div class="wrap">{label}</div>{float}',
     '.wrap{float:left;width:80px;height:40px}.label{margin-left:0}.float{margin-left:-80px}', False),
    ('inline-group', '<div class="wrap">{label}</div><div class="cover"></div>',
     '.wrap{display:inline-block;width:80px;height:40px}.label{float:left;margin-left:0}'
     '.cover{display:inline-block;margin-left:-80px}', False),
    ('inline-group-reverse', '<div class="cover"></div><div class="wrap">{label}</div>',
     '.wrap{display:inline-block;width:80px;height:40px;margin-left:-80px}.label{float:left;margin-left:0}'
     '.cover{display:inline-block}', True),
    ('inline-table', '{float}<table class="wrap"><tr><td>{label}</td></tr></table>',
     '.wrap{display:inline-table;width:80px;margin-left:-80px;border-spacing:0;vertical-align:top}'
     'td{padding:0}.label{margin-left:0}', True),
    ('inline-flex', '{float}<div class="wrap">{label}</div>',
     '.wrap{display:inline-flex;width:80px;margin-left:-80px;vertical-align:top}.label{margin-left:0}', True),
    ('inline-grid', '{float}<div class="wrap">{label}</div>',
     '.wrap{display:inline-grid;width:80px;margin-left:-80px;vertical-align:top}.label{margin-left:0}', True),
    ('flex-item-group', '<div class="wrap"><div class="item">{label}</div><div class="cover"></div></div>',
     '.wrap{display:flex;width:160px}.item{width:80px;height:40px}.label{float:left;margin-left:0}'
     '.cover{margin-left:-80px}', False),
    ('grid-item-group', '<div class="wrap"><div class="item">{label}</div><div class="cover"></div></div>',
     '.wrap{display:grid;grid-template-columns:80px 80px}.item{height:40px}.label{float:left;margin-left:0}'
     '.cover{margin-left:-80px}', False),
    ('clipped-float', '<div class="wrap">{label}</div>',
     '.wrap{overflow:hidden;width:25px;height:40px}.label{float:left;margin-left:0}', False),
    ('positioned-auto', '{float}{label}', '.label{position:relative}', True),
    ('negative', '{float}{label}', '.label{position:relative;z-index:-1}', False),
    ('float-positioned', '{float}{label}', '.float{position:relative}', False),
    ('escape-positive', '<div class="wrap">{label}</div>{float}',
     '.wrap{float:left;width:80px;height:40px}.label{margin-left:0;position:relative;z-index:2}'
     '.float{margin-left:-80px;position:relative;z-index:1}', True),
    ('isolate-context', '<div class="wrap">{label}</div>{float}',
     '.wrap{float:left;width:80px;height:40px;position:relative;z-index:0}'
     '.label{margin-left:0;position:relative;z-index:2}'
     '.float{margin-left:-80px;position:relative;z-index:1}', False),
    ('offset', '<div class="wrap">{float}{label}</div>', '.wrap{padding:11px 7px}', True),
    ('clipped-inline', '{float}<div class="wrap">{label}</div>',
     '.wrap{overflow:hidden;width:25px;height:40px}.label{margin-left:-80px}', False),
    ('single-background', '{float}{label}', '.label{background:rgba(170,136,85,.5)}', True),
    ('float-in-hidden', '<div class="wrap">{float}</div>{label}',
     '.wrap{visibility:hidden;height:0}.float{visibility:visible}', True),
    ('flow-root', '<div class="wrap">{float}{label}</div>',
     '.wrap{display:flow-root;width:160px;background:#26c}', True),
]


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><style>',
             'html,body{margin:0;font-size:0;line-height:0}input{display:none}',
             '.case{position:relative;z-index:0;display:inline-block;vertical-align:top;'
             'width:160px;height:80px;margin:4px;background:white}',
             '.float{float:left;width:80px;height:40px;background:#fa8}',
             '.fill{height:40px;background:#fa8}',
             '.label{display:inline-block;vertical-align:top;width:80px;height:20px;'
             'margin-left:-80px;background:#a85}',
             '.cover{width:80px;height:40px;background:#fa8;vertical-align:top}']
    for name, _, style, _ in CASES:
        for rule in style.split('}'):
            if '{' in rule:
                selector, body = rule.split('{')
                parts.append(f'#{name} {selector}{{{body}}}')
    parts.append('</style>')
    for name, markup, _, _ in CASES:
        label = f'<label class="label" id="{name}-label" for="{name}-input"></label>'
        floated = '<div class="float"><div class="fill"></div></div>'
        parts.append(f'<input type="checkbox" id="{name}-input"><div class="case" id="{name}">'
                     + markup.format(label=label, float=floated) + '</div>')
    return '\n'.join(parts)


def main(zoom):
    binary = Path(sys.argv[1]).resolve()
    magick = sys.argv[2] if len(sys.argv) > 2 else shutil.which('magick')
    assert magick, 'Float paint order requires ImageMagick for pixel verification'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
               QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    width, height = int(850 * zoom), int(700 * zoom)
    with tempfile.TemporaryDirectory(prefix='wisp-float-paint-') as tmp:
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
        pixels = subprocess.run([magick, str(dest / 'page.png'), '-depth', '8', 'RGB:-'],
                                check=True, capture_output=True).stdout
        assert len(pixels) == width * height * 3
        for name, _, _, winner in CASES:
            boxes = elements[name + '-label']['boxes']
            assert boxes, f'{name}: label has no layout box'
            r = boxes[0]['border_box']
            x = int((r['x'] + r['width'] / 2) * zoom)
            y = int((r['y'] + r['height'] / 2) * zoom)
            assert 0 <= x < width and 0 <= y < height, (name, x, y)
            offset = (y * width + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            expected = (170, 136, 85) if winner else (255, 170, 136)
            if name == 'clipped-float':
                expected = (255, 255, 255)
            if name == 'single-background':
                # One translucent brown layer over orange. Allow one channel
                # step for 8-bit alpha quantization; a duplicated layer is
                # much darker (about 191, 144, 98).
                expected = (213, 153, 111)
            tolerance = 1 if name == 'single-background' else 0
            if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
                failures.append(f'{name}: pixel {actual}, expected {expected}')
        assert not failures, '\n'.join(failures)
    print(f'Float painting: {len(CASES)} pixel/native click cases passed at {zoom * 100:g}%.')


if __name__ == '__main__':
    for zoom in (1, 1.5):
        main(zoom)
