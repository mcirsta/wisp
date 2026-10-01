#!/usr/bin/env python3
"""Sticky constraints after updates, resize, replacement and interrupted builds."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
fixture = Path(__file__).resolve().parents[2] / 'test/sticky-lifecycle.html'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
ids = ('header', 'container', 'inside', 'replace', 'other', 'grow', 'hide')


def capture(out, page, actions, extra=()):
    args = [binary, '--capture-dir', str(out), '--capture-size', '800x600', '--capture-settle', '50',
            '--capture-timeout', '20000', *extra]
    for key in ids:
        args += ['--capture-id', key]
    for kind, value in actions:
        args += ['--capture-' + kind, value]
    p = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=30)
    assert p.returncode == 0 and 'runtime error:' not in p.stderr, p.stderr
    meta = json.loads((out / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], meta
    return json.loads((out / 'steps.json').read_text())


def elements(step):
    return {e['id']: e for e in step['elements']}


def box(step, key):
    e = elements(step)[key]
    assert e['status'] == 'ok' and e['boxes'], e
    return e['boxes'][0]


def geometry(step):
    return {e['id']: [(b['border_box'], b['layout_border_box'], b.get('sticky')) for b in e['boxes']]
            for e in step['elements']}


with tempfile.TemporaryDirectory(prefix='wisp-sticky-lifecycle-') as directory:
    tmp = Path(directory)
    # Enough unaffected content to exercise retained regions as well as the
    # explicit full-rebuild reference. It also supplies line-cache candidates.
    page = tmp / 'page.html'
    page.write_text(fixture.read_text().replace('<div id="tail"></div>',
        '<div id="tail">' + '<p>Unchanged reading content.</p>' * 100 + '</div>'))
    actions = [('scroll', '0,500'), ('click-label', 'grow'), ('click-label', 'grow'),
               ('click-label', 'hide'), ('click-label', 'hide'),
               ('resize', '800x480'), ('resize', '800x600')]
    actions += [('click-label', 'grow'), ('click-label', 'grow')] * 4
    actions += [('scroll', '0,700'), ('scroll', '0,500')]
    reference = capture(tmp / 'full', page, actions, ('--capture-force-full-restyle',))
    for name, flags in (('retained', ()), ('uncached', ('--capture-no-inline-layout-reuse', '--capture-no-intrinsic-reuse'))):
        steps = capture(tmp / name, page, actions, flags)
        assert len(steps) == len(reference) == len(actions) + 1
        for i, (step, full) in enumerate(zip(steps, reference)):
            assert step['sticky_layout_valid'], (name, i)
            assert geometry(step) == geometry(full), (name, i, geometry(step), geometry(full))
            assert step['scroll'] == full['scroll'], (name, i, step['scroll'], full['scroll'])
            if i:
                assert step['scroll']['y'] >= 500, (name, i, step['scroll'])
            if elements(step)['header']['boxes']:
                header = box(step, 'header')
                sy = step['scroll']['y']
                grown = elements(step)['grow']['state']['checked']
                expected = 440 if grown else sy + (48 if i == 6 else 60)
                if i == 0:
                    expected = 200
                assert header['border_box']['y'] == expected, (name, i, header, expected)
                assert header['border_box']['height'] == (80 if grown else 60), (name, i)
            else:
                assert i == 4, (name, i)
            assert box(step, 'other')['border_box']['y'] == (200 if i == 0 else step['scroll']['y'] + 120), (name, i)
            if 10 <= i <= 15:
                assert step['tree_allocations'] == steps[10 + (i - 10) % 2]['tree_allocations'], (name, i)
                assert box(step, 'other')['box_address'] == box(steps[9], 'other')['box_address'], (name, i)
                if step.get('restyle_profile'):
                    assert step['restyle_profile']['update_mode'] == 'partial_boxes', (name, i, step['restyle_profile'])
        assert (tmp / name / 'page.png').read_bytes() == (tmp / 'full' / 'page.png').read_bytes(), name
        if steps[0].get('render_profile'):
            for before, after in ((0, 1), (15, 16), (16, 17)):
                assert steps[after]['render_profile']['layout_calls'] == steps[before]['render_profile']['layout_calls'], (name, before, after)

    for zoom, y in ((100, 70), (150, 50)):
        steps = capture(tmp / f'selection-{zoom}', page,
                        [('scroll', '0,500'), ('drag', f'0,{y},300,{y}')], (f'--scale={zoom}',))
        assert steps[-1].get('selection', '').strip() == 'Sticky selectable text', (zoom, steps[-1].get('selection'))
        assert steps[-1]['scroll'] == steps[1]['scroll'], (zoom, steps[-1]['scroll'])
    static = tmp / 'static.html'
    static.write_text(page.read_text().replace('</style>', '#header {position:static}</style>'))
    steps = capture(tmp / 'selection-static', static, [('drag', '0,210,300,210')])
    assert steps[-1].get('selection', '').strip() == 'Sticky selectable text', steps[-1].get('selection')

    if '--interruptions' in sys.argv:
        # Reuse the existing real-window interruption hooks, with live sticky
        # records and a nonzero viewport offset when construction is cancelled.
        page.write_text(page.read_text().replace('<div id="replace">', '<div id="replace">' + '<span></span>' * 256)
                        + '<div>' + '<p>Unchanged outside the replacement.</p>' * 1000 + '</div>')
        for mode in ('close', 'navigate', 'shutdown', 'shutdown-full'):
            out = tmp / mode
            args = [binary, '--capture-dir', str(out), '--capture-size', '800x600', '--capture-settle', '50',
                    '--capture-timeout', '20000', '--capture-interrupt-restyle', mode,
                    '--capture-scroll', '0,500', '--capture-click-label', 'grow', '--capture-click-label', 'grow',
                    '--capture-click-label', 'grow', '--capture-click-label', 'grow', str(page)]
            p = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
            assert p.returncode == 0 and 'runtime error:' not in p.stderr, (mode, p.stderr)
            data = json.loads((out / 'interruption.json').read_text())
            assert data['result'] == 'complete' and data['closed_during_build'], (mode, data)
            assert data['before']['conversion'] and data['before']['building'], (mode, data)
            if mode == 'navigate':
                assert data['current_url'] == 'about:blank', data

print('Sticky updates: stuck geometry, container limits, hide/reveal, resize, retained/full parity and lifetimes passed.')
