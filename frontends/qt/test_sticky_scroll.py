#!/usr/bin/env python3
"""Browser-referenced sticky scrolling, layout stability and visible paint/hits."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, magick = sys.argv[1:3]
root = Path(__file__).resolve().parents[2]
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1', QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
# Chromium 153 and Firefox 157 measured sequences from sticky-position.html.
# The last two cases use a centered item (also checked in Chromium), isolating
# sticky from Wisp's existing RTL flex placement difference. Wisp adds horizontal
# scrollbar space to the CSS height; these dimensions preserve the same 200x120
# client area, rather than matching the reference's outer height.
references = {
    'top': [80, 10, 10, -20, -90, 80], 'bottom': [70, -20, -220, -300, -370, 70],
    'both': [60, 10, 10, -20, -90, 60], 'tall-top': [80, 10, -80, -160, -230, 80],
    'tall-bottom': [0, -70, -220, -300, -370, 0], 'tall-both': [10, 10, -80, -160, -230, 10],
    'margin': [80, 10, 10, -50, -120, 80], 'negative-margin': [80, 10, 10, 0, -70, 80],
    'left': [80, 10, 10, 10, -10, 80], 'right': [250, 150, -50, -130, -200, 250],
    'wide': [80, 10, -40, -120, -190, 80], 'wide-rtl': [80, -20, -220, -300, -370, 80],
    'wide-right': [0, -50, -220, -300, -370, 0], 'wide-rtl-left': [80, 10, -140, -220, -290, 80],
    'wide-rtl-both': [0, -60, -140, -220, -290, 0],
}
scrolls = [0, 100, 300, 380, 450, 0]


def run(out, page, ids, actions, zoom=1, extra=()):
    args = [binary, '--capture-dir', str(out), '--capture-size', '800x600', '--capture-settle', '50',
            '--capture-timeout', '15000', f'--scale={int(zoom * 100)}', *extra]
    for key in ids:
        args += ['--capture-id', key]
    for kind, value in actions:
        args += ['--capture-' + kind, value]
    process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=30)
    assert process.returncode == 0 and 'runtime error:' not in process.stderr, process.stderr
    meta = json.loads((out / 'capture.json').read_text())
    assert meta['load_complete'] and meta['result'] == 'complete', meta
    return json.loads((out / 'steps.json').read_text()), meta


def box(step, key):
    element = next(e for e in step['elements'] if e['id'] == key)
    assert element['status'] == 'ok' and element.get('boxes'), element
    return element['boxes'][0]


with tempfile.TemporaryDirectory(prefix='wisp-sticky-scroll-') as directory:
    tmp = Path(directory)
    source = (root / 'test/sticky-position.html').read_text()
    for name, positions in references.items():
        horizontal = name in ('left', 'right') or name.startswith('wide')
        axis = 'x' if horizontal else 'y'
        page = tmp / f'{name}.html'
        page.write_text(source.replace(f'<div class="case"><div id="{name}-scroll"',
            f'<div class="case" id="active"><div id="{name}-scroll"').replace('</style>',
            '.case {display:none} #active {display:block} .scroller {width:216px;height:120px;overflow:scroll}'
            '.axis-row.rtl {width:400px;justify-content:center} .axis-row.rtl .axis-before {width:0}</style>'))
        actions = [('scroll-element', f'{name}-scroll,{s if horizontal else 0},{0 if horizontal else s}') for s in scrolls]
        steps, _ = run(tmp / name, page, (name, name + '-scroll'), actions)
        initial_layout = box(steps[0], name)['layout_border_box']
        for i, (step, expected, scroll) in enumerate(zip(steps[1:], positions, scrolls)):
            header, port = box(step, name), box(step, name + '-scroll')
            assert port['scrollbars'][axis] == scroll, (name, i, port['scrollbars'])
            actual = header['border_box'][axis] - port['padding_box'][axis]
            assert actual == expected, (name, i, actual, expected, header['sticky'])
            assert header['layout_border_box'] == initial_layout, (name, i)
            assert step['tree_allocations'] == steps[0]['tree_allocations'], (name, i)
        assert box(steps[-1], name)['border_box'] == box(steps[0], name)['border_box'], name

    fixture = root / 'test/sticky-scroll.html'
    ids = ('header', 'header-link', 'under', 'absolute', 'floating', 'fixed', 'picture', 'check', 'field', 'frame',
           'port', 'outer', 'inner', 'clipped')

    def pixels(out, probes, zoom=1):
        raw = subprocess.run([magick, str(out / 'page.png'), '-depth', '8', 'RGB:-'],
                             check=True, capture_output=True).stdout
        assert len(raw) == 800 * 600 * 3
        for x, y, colour in probes:
            px, py = int(x * zoom), int(y * zoom)
            at = (py * 800 + px) * 3
            assert tuple(raw[at:at + 3]) == colour, (out.name, x, y, tuple(raw[at:at + 3]), colour)

    green, blue, pink, white = (34, 136, 68), (34, 85, 204), (204, 34, 85), (255, 255, 255)
    initial, _ = run(tmp / 'paint-initial', fixture, ids, [('scroll', '0,0')])
    pixels(tmp / 'paint-initial', [(510, 30, green), (610, 30, white)])
    for zoom in (1, 1.5):
        out = tmp / f'paint-{zoom}'
        steps, _ = run(out, fixture, ids, [('scroll', '0,150')], zoom)
        step = steps[-1]
        sy = step['scroll']['y']
        header = box(step, 'header')['border_box']
        assert header['y'] - sy == 10, (zoom, header, sy)
        for key, dy in (('absolute', 0), ('floating', 20), ('picture', 40), ('frame', 25), ('check', 0), ('field', 0)):
            assert box(step, key)['border_box']['y'] - header['y'] == dy, (zoom, key)
        assert box(step, 'fixed')['border_box']['y'] - sy == 10
        pixels(out, [(5, 15, green), (85, 15, blue), (85, 55, blue), (285, 35, pink), (5, 95, (221, 51, 51))], zoom)
        # A fixed viewport click must activate the painted header's label/control,
        # not the overlapping in-flow link. Frame input must move with its owner.
        actions = [('scroll', '0,150'), ('click-point', '90,60'), ('click-point', '116,16'),
                   ('click-point', '160,18'), ('type', 'sticky'), ('click-point', '226,41')]
        out = tmp / f'input-{zoom}'
        steps, meta = run(out, fixture, ids, actions, zoom)
        elements = {e['id']: e for e in steps[-1]['elements']}
        assert elements['check']['state']['checked'], (zoom, elements['check'])
        assert elements['field']['state']['value'] == 'sticky', (zoom, elements['field']['state'])
        assert steps[-1]['scroll'] == steps[1]['scroll'], (zoom, steps[-1]['scroll'])
        pixels(out, [(226, 59, blue)], zoom)
        steps, meta = run(tmp / f'link-{zoom}', fixture, ids,
                          [('scroll', '0,150'), ('click-point', '20,15')], zoom)
        # The content-cache URL omits fragments. The header target is at zero;
        # activating the covered link instead would scroll towards the page end.
        assert steps[-1]['scroll']['y'] == 0, (zoom, steps[-1]['scroll'])
        out = tmp / f'frame-scroll-{zoom}'
        steps, _ = run(out, fixture, ids,
                       [('scroll', '0,150'), ('scroll-point', '226,41,0,30')], zoom)
        assert steps[-1]['scroll'] == steps[1]['scroll'], (zoom, steps[-1]['scroll'])
        # Scrolling the child replaces the checkbox at this point with red
        # content, without moving the document or its sticky frame owner.
        pixels(out, [(226, 41, (221, 51, 51))], zoom)

    out = tmp / 'nested'
    steps, _ = run(out, fixture, ids, [('scroll-element', 'port,0,100')])
    outer, inner, port = (box(steps[-1], key)['border_box'] for key in ('outer', 'inner', 'port'))
    assert outer['y'] - port['y'] == 10 and inner['y'] - port['y'] == 25, (outer, inner, port)
    pixels(out, [(345, 130, blue), (390, 115, green)])
    out = tmp / 'limit'
    steps, _ = run(out, fixture, ids, [('scroll', '0,550')])
    assert box(steps[-1], 'header')['border_box']['y'] == 500, steps[-1]['scroll']
    pixels(out, [(5, 10, green), (5, 35, white)])
    out = tmp / 'roundtrip'
    steps, _ = run(out, fixture, ids, [('scroll', '0,150'), ('scroll', '0,550'), ('scroll', '0,0')])
    assert (out / 'page.png').read_bytes() == (tmp / 'paint-initial' / 'page.png').read_bytes()
    assert steps[-1]['tree_allocations'] == steps[0]['tree_allocations']

    contexts = root / 'test/sticky-contexts.html'
    items = ('flex-stretch', 'flex-start', 'grid-stretch', 'grid-start', 'clipped', 'outer-port', 'inner-port', 'nested')
    steps, _ = run(tmp / 'items', contexts, items, [('scroll', '0,150'), ('scroll', '0,380')])
    for i, y in enumerate((80, 160, 340)):
        for layout in ('flex', 'grid'):
            stretched = box(steps[i], layout + '-stretch')['border_box']
            start = box(steps[i], layout + '-start')['border_box']
            assert (stretched['y'], stretched['height']) == (80, 300), (i, layout, stretched)
            assert (start['y'], start['height']) == (y, 40), (i, layout, start)
    out = tmp / 'stacking'
    run(out, contexts, items, [('scroll', '0,180')])
    pixels(out, [(505, 30, (221, 51, 51)), (625, 30, green)])
    for name, x, end in (('negative', 505, 1400), ('positive', 625, 0)):
        steps, _ = run(tmp / name, contexts, items, [('scroll', '0,180'), ('click-point', f'{x},30')])
        assert steps[-1]['scroll']['y'] == end, (name, steps[-1]['scroll'])
    for zoom in (1, 1.5):
        out = tmp / f'css-clip-{zoom}'
        run(out, contexts, items, [('scroll', f'0,{int(550 * zoom)}')], zoom)
        pixels(out, [(5, 15, blue), (5, 35, white), (85, 15, white)], zoom)
    out = tmp / 'two-scrollports'
    steps, _ = run(out, contexts, items,
        [('scroll', '0,850'), ('scroll-element', 'inner-port,0,100'), ('scroll-element', 'outer-port,0,40')])
    last = steps[-1]
    inner, nested = box(last, 'inner-port'), box(last, 'nested')
    assert nested['border_box']['y'] - inner['padding_box']['y'] == 10, (inner, nested)
    assert nested['layout_border_box'] == box(steps[0], 'nested')['layout_border_box']
    pixels(out, [(5, 75, green)])
    for step in steps[1:]:
        if step.get('render_profile'):
            assert step['render_profile']['layout_calls'] == steps[0]['render_profile']['layout_calls'], step['render_profile']

print('Sticky scrolling: browser geometry, paint, clipping, stacking, native/frame input and round trips passed.')
