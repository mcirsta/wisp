#!/usr/bin/env python3
"""Unbreakable inline runs contribute their complete shrink-to-fit width."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def patterns():
    # The bundled font advances 12px per H and 5px per space at 20px.
    # name, markup, min-content, max-content (excluding the target's own edges).
    word, half = 'H' * 20, 'H' * 10
    yield 'plain', word, 240, 240
    yield 'padding', f'<span class="edge">{word}</span>', 250, 250
    yield 'border', f'<span style="border:5px solid">{word}</span>', 250, 250
    yield 'margin', f'<span style="margin:0 5px">{word}</span>', 250, 250
    yield 'nested', f'<span class="edge"><span class="edge">{word}</span></span>', 260, 260
    yield 'joined', f'<span class="edge">{half}</span><span class="edge">{half}</span>', 260, 260
    yield 'plain-joined', f'<span>{half}</span><span>{half}</span>', 240, 240
    yield 'prefix', f'{half}<span class="edge">{half}</span>', 250, 250
    yield 'internal-space', f'<span class="edge">{half} HHHHH</span>', 125, 195
    yield 'joined-tail', f'HH {"H" * 8}<span class="edge">HHHHH</span>', 166, 195
    yield 'between', f'<span class="edge">{half}</span> <span class="edge">{half}</span>', 130, 265
    yield 'space-at-end', f'<span class="edge">{half} </span>HHHHH', 130, 195
    yield 'space-at-start', f'{half}<span class="edge"> HHHHH</span>', 125, 195
    yield 'empty', '<span class="edge"></span>', 10, 10
    yield 'empty-in-word', f'{half}<span class="edge"></span>{half}', 250, 250
    yield 'empty-after-space', 'HH <span style="padding:0 30px"></span>HHHHH', 60, 149
    yield 'nowrap', f'<span class="edge" style="white-space:nowrap">{half} HHHHH</span>', 195, 195
    yield 'nowrap-joined', f'<span style="white-space:nowrap"><span>{half}</span> <span>{half}</span></span>', 245, 245
    yield 'pre', f'<span class="edge" style="white-space:pre">{half} HHHHH</span>', 195, 195
    yield 'br', f'<span class="edge">{half}<br>{word}</span>', 245, 245
    yield 'atomic', '<span class="edge"><i style="width:240px"></i></span>', 250, 250
    yield 'two-atomics', '<span class="edge"><i></i><i></i></span>', 125, 250
    yield 'positioned', f'{half}<span style="position:absolute">X</span>{half}', 240, 240
    yield 'percentage', f'<span style="padding:0 10%">{word}</span>', 240, 240
    yield 'calc', f'<span style="padding:0 calc(2px + 3px)">{word}</span>', 250, 250


def cases():
    for kind in ('inline', 'float', 'flex', 'table'):
        for name, markup, minimum, maximum in patterns():
            yield kind + '-' + name, kind, markup, minimum, maximum


def fixture_html():
    font = (Path(__file__).resolve().parents[2] / 'test/fonts/normal-400.ttf').as_uri()
    parts = [f'<!doctype html><meta charset="utf-8"><title>Inline intrinsic widths</title><style>'
             f'@font-face{{font-family:Test;src:url("{font}")}}'
             'body{margin:0;font:20px/20px Test}.case{display:flow-root;margin:10px;border:1px solid}'
             '.edge{padding:0 5px;background:#fed}i{display:inline-block;width:120px;height:20px;vertical-align:top}'
             '.inline{display:inline-block}.float{float:left}.flex{display:flex}.flex>div{flex:0 1 auto}'
             'table{border-spacing:0}td{padding:0}</style>']
    for name, kind, markup, _, _ in cases():
        if kind == 'table':
            content = f'<table id="{name}"><tr><td>{markup}</td></tr></table>'
        elif kind == 'flex':
            content = f'<div class="flex"><div id="{name}">{markup}</div></div>'
        else:
            content = f'<div class="{kind}" id="{name}">{markup}</div>'
        parts.append(f'<section class="case">{content}</section>')
    return '\n'.join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
               QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    with tempfile.TemporaryDirectory(prefix='wisp-inline-intrinsics-') as tmp:
        fixture = Path(tmp) / 'intrinsics.html'
        fixture.write_text(fixture_html())
        dest = Path(tmp) / 'capture'
        # 100px forces min-content, 400px allows max-content, 200px tests the
        # intermediate shrink-to-fit width. Revisit 100px to exercise reuse.
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '122x800',
                '--capture-settle', '100', '--capture-timeout', '8000', '--enable_javascript=0',
                '--capture-resize', '422x800', '--capture-resize', '222x800', '--capture-resize', '122x800']
        for name, *_ in cases():
            args += ['--capture-id', name]
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == 4, len(steps)
        for index, (step, available) in enumerate(zip(steps, (100, 400, 200, 100))):
            assert all(e['status'] == 'ok' and e.get('boxes') for e in step['elements']), step['elements']
            boxes = {e['id']: e['boxes'][0]['border_box'] for e in step['elements']}
            for name, _, _, minimum, maximum in cases():
                expected = min(maximum, max(minimum, available))
                actual = boxes[name]['width']
                assert actual == expected, (index, name, actual, expected)
        assert steps[0]['elements'] == steps[-1]['elements'], 'Resize round trip changed geometry'
    print(f'Inline intrinsics: {len(list(cases()))} cases passed at three widths and after resizing.')


if __name__ == '__main__':
    main()
