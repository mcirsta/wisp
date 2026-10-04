#!/usr/bin/env python3
"""Overflowing lines use the start edge; fitting lines retain text-align."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def cases():
    for direction in ('ltr', 'rtl'):
        for align in ('left', 'center', 'right', 'default'):
            for kind, width in (('text', 250), ('atomic', 250), ('exact', 200)):
                yield f'{direction}-{align}-{kind}', direction, align, kind, width


def fixture_html():
    font = (Path(__file__).resolve().parents[2] / 'test/fonts/normal-400.ttf').as_uri()
    parts = [f'<!doctype html><meta charset="utf-8"><title>Inline alignment</title><style>'
             f'@font-face{{font-family:Test;src:url("{font}")}}'
             'body{margin:0;font:20px/20px Test}.case{display:flow-root;margin:10px;border:1px solid}'
             '.edge{padding:0 5px;background:#fed}i{display:inline-block;height:20px;'
             'vertical-align:top;background:#cde}</style>']
    for name, direction, align, kind, width in cases():
        alignment = '' if align == 'default' else f'text-align:{align};'
        # A single Latin word isolates line alignment from bidi reordering.
        # Its trailing collapsed space must be trimmed before alignment.
        markup = (f'<span class="edge">{"H" * 20} </span>' if kind == 'text' else
                  f'<i id="{name}-item" style="width:{width}px"></i>')
        parts.append(f'<section class="case" id="{name}" style="{alignment}direction:{direction}">'
                     f'{markup}</section>')
    return '\n'.join(parts)


def expected_x(direction, align, width, available):
    if width > available or align == 'default':
        return available - width if direction == 'rtl' else 0
    if align == 'right':
        return available - width
    if align == 'center':
        return (available - width) // 2
    return 0


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
               QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    with tempfile.TemporaryDirectory(prefix='wisp-inline-alignment-') as tmp:
        fixture = Path(tmp) / 'alignment.html'
        fixture.write_text(fixture_html())
        dest = Path(tmp) / 'capture'
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '222x800',
                '--capture-settle', '100', '--capture-timeout', '8000', '--enable_javascript=0',
                '--capture-resize', '422x800', '--capture-resize', '222x800']
        for name, _, _, kind, _ in cases():
            args += ['--capture-id', name]
            if kind != 'text':
                args += ['--capture-id', name + '-item']
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == 3, len(steps)
        for index, (step, available) in enumerate(zip(steps, (200, 400, 200))):
            assert all(e['status'] == 'ok' and e.get('boxes') for e in step['elements']), step['elements']
            boxes = {e['id']: e['boxes'][0]['border_box'] for e in step['elements']}
            for name, direction, align, kind, width in cases():
                case = boxes[name]
                assert case['height'] == 22 and case['width'] == available + 2, (index, name, case)
                if kind != 'text':
                    item = boxes[name + '-item']
                    actual = item['x'] - case['x'] - 1
                    expected = expected_x(direction, align, width, available)
                    assert actual == expected and item['width'] == width, (index, name, item, expected)
        assert steps[0]['elements'] == steps[-1]['elements'], 'Resize round trip changed geometry'

        # Inline span inspection describes fragments, so verify actual text
        # positions in the final narrow layout using the box tree instead.
        expected = {name: [(expected_x(direction, align, width, 200) + 5, 0, 240, 20)]
                    for name, direction, align, kind, width in cases() if kind == 'text'}
        found, owner = {name: [] for name in expected}, None
        for line in (dest / 'boxes.txt').read_text().splitlines():
            match = re.search(r'\bID:(\S+)', line)
            if match:
                owner = match[1]
            if owner in expected and ' TEXT ' in line:
                match = re.search(r'\bx(-?\d+) y(-?\d+) w(\d+) h(\d+)', line)
                assert match, (owner, line)
                found[owner].append(tuple(map(int, match.groups())))
        assert found == expected, (found, expected)
    print('Inline alignment: 24 LTR/RTL cases passed before and after resizing.')


if __name__ == '__main__':
    main()
