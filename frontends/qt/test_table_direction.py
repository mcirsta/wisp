#!/usr/bin/env python3
"""CSS 2.2 17.5 and 17.6.2.1: table direction controls columns and border ties.

Geometry follows fixed content widths, border halves and border spacing, not
browser snapshots. Row/cell direction must not reorder the table's columns.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# (name, border model, rows of (id suffix, logical column, colspan, rowspan, content width))
PLAIN = [[('a', 0, 1, 1, 20), ('b', 1, 1, 1, 40), ('c', 2, 1, 1, 60)]]
SPANS = [[('a', 0, 1, 1, 20), ('b', 1, 1, 1, 40), ('c', 2, 1, 1, 60)],
         [('d', 0, 2, 1, 20), ('e', 2, 1, 1, 60)]]
ROWSPAN = [[('a', 0, 1, 2, 20), ('b', 1, 1, 1, 40), ('c', 2, 1, 1, 60)],
           [('d', 1, 2, 1, 20)]]
TWO = [[('a', 0, 1, 1, 20), ('b', 1, 1, 1, 20)]]
CASES = [('separate', 'separate', PLAIN), ('collapse', 'collapse', PLAIN),
         ('odd', 'odd', PLAIN), ('colspan', 'collapse', SPANS),
         ('rowspan', 'collapse', ROWSPAN), ('separate-span', 'separate', ROWSPAN),
         ('override', 'collapse', PLAIN), ('inline', 'collapse', PLAIN),
         ('nested', 'collapse', PLAIN), ('tie', 'collapse', TWO),
         ('asymmetric', 'collapse', TWO)]


def fixture_html():
    parts = ['''<!doctype html><meta charset="utf-8"><title>Table column direction</title>
<style>
/* CSS 2.2 17.5: the TABLE direction determines column order; source column
   numbering is unchanged. 17.6.2.1: ties prefer the physical leading cell. */
html,body{margin:0;font-size:0;line-height:0;background:white}
input{display:none} #switch-label{display:block;width:40px;height:20px;background:#aaa}
.case{display:inline-block;vertical-align:top;width:210px;height:100px;padding:8px}
table{margin:0;border:0;border-collapse:collapse;border-spacing:0}
td{padding:0;border:2px solid black;vertical-align:top}
td>label{display:block;height:20px;background:#ffffcc}
.separate{border-collapse:separate;border-spacing:3px 5px}.separate td{border-width:1px}
.odd td{border-width:3px}.override tr,.override td{direction:ltr}
.inline{display:inline-table}
.tie .a{border-color:#ff0000}.tie .b{border-color:#0000ff}
.asymmetric .a{border-left:6px solid #00ff00;border-right:10px solid #ff0000}
.asymmetric .b{border-left:4px solid #0000ff;border-right:8px solid #ff00ff}
#switch:checked ~ main .live{direction:ltr !important}
.outer{direction:rtl}.outer>tbody>tr>td{border:0;padding:3px}
</style><input id="switch" type="checkbox"><label id="switch-label" for="switch"></label>
<input id="hit" type="checkbox"><main>''']
    for name, model, rows in CASES:
        for direction in ('ltr', 'rtl'):
            key = name + '-' + direction
            parts.append('<div class="case">')
            if name == 'nested':
                parts.append('<table class="outer"><tr><td>')
            # Every RTL table participates in a real RTL -> LTR -> RTL restyle.
            classes = f'{name} {model} ' + ('live' if direction == 'rtl' else '')
            parts.append(f'<table id="{key}" class="{classes}" style="direction:{direction}"><tbody>')
            for row in rows:
                parts.append('<tr>')
                for suffix, _, span, rowspan, width in row:
                    cell = key + '-' + suffix
                    parts.append(f'<td id="{cell}" class="{suffix}" colspan="{span}" rowspan="{rowspan}">'
                                 f'<label id="{cell}-label" for="hit" style="width:{width}px"></label></td>')
                parts.append('</tr>')
            parts.append('</tbody></table>')
            if name == 'nested':
                parts.append('</td></tr></table>')
            parts.append('</div>')
    return '\n'.join(parts) + '</main>'


def expected_geometry(model, rows, rtl, asymmetric=False):
    spacing_x, spacing_y = (3, 5) if model == 'separate' else (0, 0)
    outer_left, outer_right, outer_top, outer_bottom = (0, 0, 0, 0) if model == 'separate' else (
        (1, 2, 1, 2) if model == 'odd' else (1, 1, 1, 1))
    borders = 3 if model == 'odd' else 2
    widths = [cell[4] + borders for cell in rows[0]]
    if asymmetric:
        # Physical sides stay left/right while adjacency changes under RTL.
        widths = [29, 26] if rtl else [28, 29]
        outer_left, outer_right = (2, 5) if rtl else (3, 4)
    row_height = 20 + borders
    total_width = outer_left + sum(widths) + spacing_x * (len(widths) + 1) + outer_right
    total_height = outer_top + len(rows) * row_height + spacing_y * (len(rows) + 1) + outer_bottom
    expected = {}
    for row_index, row in enumerate(rows):
        for suffix, start, span, rowspan, _ in row:
            preceding = sum(widths[start + span:]) if rtl else sum(widths[:start])
            physical_col = len(widths) - start - span if rtl else start
            x = outer_left + preceding + spacing_x * (physical_col + 1)
            y = outer_top + row_index * row_height + spacing_y * (row_index + 1)
            expected[suffix] = (x, y, sum(widths[start:start + span]) + spacing_x * (span - 1),
                                rowspan * row_height + spacing_y * (rowspan - 1))
    return (total_width, total_height), expected


def main(zoom):
    binary, magick = Path(sys.argv[1]).resolve(), sys.argv[2]
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1', QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    width, height = int(800 * zoom), int(1500 * zoom)
    with tempfile.TemporaryDirectory(prefix='wisp-table-direction-') as tmp:
        fixture = Path(tmp) / 'fixture.html'
        fixture.write_text(fixture_html())
        dest = Path(tmp) / 'capture'
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', f'{width}x{height}',
                '--capture-settle', '100', '--capture-timeout', '15000', '--enable_javascript=0',
                f'--scale={int(100 * zoom)}', '--capture-id', 'switch', '--capture-id', 'hit']
        for name, _, rows in CASES:
            for direction in ('ltr', 'rtl'):
                key = name + '-' + direction
                args += ['--capture-id', key]
                for row in rows:
                    for suffix, *_ in row:
                        args += ['--capture-id', key + '-' + suffix]
        args += ['--capture-resize', f'{int(700 * zoom)}x{height}', '--capture-resize', f'{width}x{height}',
                 '--capture-click-id', 'switch-label', '--capture-click-id', 'switch-label',
                 '--capture-click-id', 'collapse-rtl-a-label', fixture.as_uri()]
        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=35)
        assert run.returncode == 0 and 'runtime error:' not in run.stderr, run.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == 6, len(steps)
        for index, step in enumerate(steps):
            elements = {e['id']: e for e in step['elements']}
            assert elements['switch']['state']['checked'] == (index == 3), (index, elements['switch'])
            assert elements['hit']['state']['checked'] == (index == 5), (index, elements['hit'])
            for name, model, rows in CASES:
                for direction in ('ltr', 'rtl'):
                    key = name + '-' + direction
                    assert elements[key].get('boxes'), (index, key, elements[key])
                    origin = elements[key]['boxes'][0]['border_box']
                    size, cells = expected_geometry(model, rows, direction == 'rtl' and index != 3,
                                                    asymmetric=name == 'asymmetric')
                    assert (origin['width'], origin['height']) == size, (index, key, origin, size)
                    for suffix, expected in cells.items():
                        box = elements[key + '-' + suffix]['boxes'][0]['border_box']
                        actual = (box['x'] - origin['x'], box['y'] - origin['y'], box['width'], box['height'])
                        assert actual == expected, (index, key, suffix, actual, expected)
        pixels = subprocess.run([magick, str(dest / 'page.png'), '-depth', '8', 'RGB:-'],
                                check=True, capture_output=True).stdout
        assert len(pixels) == width * height * 3
        elements = {e['id']: e for e in steps[-1]['elements']}
        samples = {'tie-ltr': [(22, 10, (255, 0, 0))], 'tie-rtl': [(22, 10, (255, 0, 0))],
                   'asymmetric-ltr': [(0, 10, (0, 255, 0)), (30, 10, (255, 0, 0)), (63, 10, (255, 0, 255))],
                   'asymmetric-rtl': [(0, 10, (0, 0, 255)), (27, 10, (255, 0, 255)), (61, 10, (255, 0, 0))],
                   'colspan-rtl': [(62, 10, (0, 0, 0)), (104, 32, (255, 255, 255))],
                   'rowspan-rtl': [(20, 22, (0, 0, 0)), (116, 22, (255, 255, 255))]}
        for key, probes in samples.items():
            origin = elements[key]['boxes'][0]['border_box']
            for dx, dy, expected in probes:
                x, y = int((origin['x'] + dx + .5) * zoom), int((origin['y'] + dy + .5) * zoom)
                at = (y * width + x) * 3
                actual = tuple(pixels[at:at + 3])
                assert actual == expected, (zoom, key, dx, dy, actual, expected)
    print(f'Table direction: {2 * len(CASES)} cases at {zoom * 100:g}%, resize, native direction changes and clicks.')


if __name__ == '__main__':
    main(1)
    main(1.5)
