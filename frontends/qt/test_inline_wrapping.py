#!/usr/bin/env python3
"""Decorations and DOM boundaries cannot split an otherwise unbreakable word."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def cases():
    # (name, markup, line bottom at 200px, line bottom at 400px).
    # The bundled font advances 12px per H; line-height is explicitly 20px.
    word = 'H' * 20
    for prop in ('padding', 'margin', 'border'):
        for side in ('left', 'right', 'both'):
            value = '5px solid' if prop == 'border' else '5px'
            style = ';'.join(f'{prop}-{edge}:{value}' for edge in
                             (('left', 'right') if side == 'both' else (side,)))
            yield f'{prop}-{side}', f'<span style="{style}">{word}</span>', 20, 20
    yield 'nested', f'<span class="edge"><span class="edge">{word}</span></span>', 20, 20
    yield 'large-opening', '<span style="padding-left:220px">HHHH</span>', 20, 20
    yield 'empty-decorated', '<span style="padding:0 150px"></span>', 20, 20
    yield 'closing-overflow', f'<span style="padding-right:10px">{"H" * 16}</span>', 20, 20
    yield 'cross-node-word', f'<span class="edge">{"H" * 10}</span><span class="edge">{"H" * 10}</span>', 20, 20
    yield 'text-before-span', f'{"H" * 10}<span class="edge">{"H" * 10}</span>', 20, 20
    yield 'space-before', f'HH <span class="edge">{word}</span>', 40, 20
    yield 'space-inside', f'<span class="edge">HH {word}</span>', 40, 20
    yield 'space-at-opening', f'HH<span class="edge"> {word}</span>', 40, 20
    yield 'space-at-closing', f'<span class="edge">{"H" * 16} </span>HH', 40, 20
    yield 'closing-backtrack', f'<span style="padding-right:20px">HH {"H" * 13}</span>', 40, 20
    yield 'cross-node-backtrack', f'HH {"H" * 8}<span style="padding-right:20px">{"H" * 5}</span>', 40, 20
    yield 'between-spans', f'<span class="edge">{"H" * 10}</span> <span class="edge">{"H" * 10}</span>', 40, 20
    yield 'empty-in-word', f'{"H" * 10}<span class="edge"></span>{"H" * 10}', 20, 20
    yield 'empty-after-space', f'HH <span class="edge"></span>{word}', 40, 20
    yield 'collapsed-space', f'<span class="edge"> </span>{word}', 20, 20
    yield 'nowrap', f'<span class="edge" style="white-space:nowrap">HH {word}</span>', 20, 20
    yield 'pre', f'<span class="edge" style="white-space:pre">HH {word}</span>', 20, 20
    yield 'br', f'<span class="edge">{word}<br>HH</span>', 40, 40
    yield 'atomic-oversize', '<span class="edge"><i style="width:240px"></i></span>', 20, 20
    yield 'atomic-before', f'<span class="edge"><i style="width:40px"></i>{"H" * 16}</span>', 40, 20
    yield 'atomic-after', f'<span class="edge">{"H" * 16}<i style="width:40px"></i></span>', 40, 20
    yield 'atomic-between', '<i></i><i></i><i></i><i></i>', 40, 20
    yield 'float-before', '<b class="floating"></b><span class="edge">HHHHH</span>', 60, 20
    yield 'float-oversize', f'<b class="floating"></b><span class="edge">{word}</span>', 60, 20
    yield 'float-closing', f'<b class="floating"></b><span class="edge">{"H" * 4}</span>', 60, 20
    yield 'unequal-floats', ('<b class="floating" style="width:90px"></b>'
                           '<b class="floating" style="float:right;width:90px;height:80px"></b>'
                           '<span class="edge">HHHH</span>'), 60, 20
    yield 'float-in-word', '<span class="edge">HH<b class="floating"></b>HHHHHHHHHH</span>', 20, 20
    yield 'float-backtrack', ('HH <span class="edge">HH<b class="floating" style="width:100px"></b>'
                             'HHHHHHHHHH</span>'), 40, 20
    yield 'positioned-in-word', f'{"H" * 10}<span style="position:absolute">X</span>{"H" * 10}', 20, 20
    yield 'center', f'<div style="text-align:center">HH <span class="edge">{"H" * 14}</span></div>', 40, 20
    yield 'right', f'<div style="text-align:right">HH <span class="edge">{"H" * 14}</span></div>', 40, 20


def fixture_html():
    font = (Path(__file__).resolve().parents[2] / 'test/fonts/normal-400.ttf').as_uri()
    parts = [f'<!doctype html><meta charset="utf-8"><title>Inline wrapping</title><style>'
             f'@font-face{{font-family:Test;src:url("{font}")}}'
             'body{margin:0;font:20px/20px Test}.case{display:flow-root;margin:10px;border:1px solid}'
             '.edge{padding:0 5px;background:#fed}i{display:inline-block;width:60px;height:20px;'
             'vertical-align:top;background:#cde}.floating{float:left;width:150px;height:40px;background:#ddd}'
             'b{font-weight:normal}</style>']
    for name, markup, _, _ in cases():
        parts.append(f'<section class="case" id="{name}-case">{markup}'
                     f'<div id="{name}-after" style="height:8px;background:#6a6"></div></section>')
    return '\n'.join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
               QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    with tempfile.TemporaryDirectory(prefix='wisp-inline-wrapping-') as tmp:
        fixture = Path(tmp) / 'inline-wrapping.html'
        fixture.write_text(fixture_html())
        dest = Path(tmp) / 'capture'
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '222x800',
                '--capture-settle', '100', '--capture-timeout', '8000', '--enable_javascript=0',
                '--capture-resize', '422x800', '--capture-resize', '222x800']
        for name, *_ in cases():
            args += ['--capture-id', name + '-case', '--capture-id', name + '-after']
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == 3, len(steps)
        for index, step in enumerate(steps):
            assert all(e['status'] == 'ok' and e.get('boxes') for e in step['elements']), step['elements']
            boxes = {e['id']: e['boxes'][0]['border_box'] for e in step['elements']}
            for name, _, narrow, wide in cases():
                case, after = boxes[name + '-case'], boxes[name + '-after']
                actual = after['y'] - case['y'] - 1
                expected = wide if index == 1 else narrow
                assert actual == expected, (index, name, actual, expected)
        assert steps[0]['elements'] == steps[-1]['elements'], 'Resize round trip changed geometry'

        # Also check the text itself: a correct total height must not hide an
        # overflowing word split between fragments or missing inline edges.
        expected = {f'{prop}-{side}-case': [(0 if side == 'right' else 5, 0, 240, 20)]
                    for prop in ('padding', 'margin', 'border') for side in ('left', 'right', 'both')}
        expected.update({
            'nested-case': [(10, 0, 240, 20)], 'large-opening-case': [(220, 0, 48, 20)],
            'closing-backtrack-case': [(0, 0, 24, 20), (0, 20, 156, 20)],
            'cross-node-backtrack-case': [(0, 0, 24, 20), (0, 20, 96, 20), (96, 20, 60, 20)],
            'space-before-case': [(0, 0, 24, 20), (5, 20, 240, 20)],
            'space-inside-case': [(5, 0, 24, 20), (0, 20, 240, 20)],
            'space-at-opening-case': [(0, 0, 24, 20), (0, 20, 240, 20)],
            'empty-after-space-case': [(0, 0, 24, 20), (0, 20, 240, 20)],
            'float-in-word-case': [(155, 0, 24, 20), (179, 0, 120, 20)],
            'float-backtrack-case': [(0, 0, 24, 20), (105, 20, 24, 20), (129, 20, 120, 20)],
            'center-case': [(88, 0, 24, 20), (16, 20, 168, 20)],
            'right-case': [(176, 0, 24, 20), (27, 20, 168, 20)],
        })
        found, owner = {name: [] for name in expected}, None
        for line in (dest / 'boxes.txt').read_text().splitlines():
            match = re.search(r'\bID:(\S+)', line)
            if match and match[1].endswith('-case'):
                owner = match[1]
            if owner in expected and ' TEXT ' in line:
                match = re.search(r'\bx(-?\d+) y(-?\d+) w(\d+) h(\d+)', line)
                assert match, (owner, line)
                found[owner].append(tuple(map(int, match.groups())))
        for name in expected:
            assert found[name] == expected[name], (name, found[name], expected[name])
    print(f'Inline wrapping: {len(list(cases()))} cases passed before and after resizing.')


if __name__ == '__main__':
    main()
