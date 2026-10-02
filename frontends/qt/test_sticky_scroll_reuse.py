#!/usr/bin/env python3
"""Native copied-pixel comparisons: QWidget.grab() would hide stale pixels."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix='wisp-sticky-reuse-') as directory:
    root = Path(directory)

    def run(name, page, actions, dpr, full=False):
        dest = root / name
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '800x600',
                '--capture-screen', '--capture-settle', '50', '--capture-timeout', '15000']
        if full:
            args += ['--capture-no-scroll-blit']
        for kind, value in actions:
            args += ['--capture-' + kind, value]
        process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=25)
        assert process.returncode == 0 and 'runtime error:' not in process.stderr, (name, process.stderr[-4000:])
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['load_complete'] and meta['result'] == 'complete', (name, meta)
        return (dest / 'page.png').read_bytes(), json.loads((dest / 'steps.json').read_text())

    def compare(name, page, actions, dpr=1, reuse=True, last_reuse=True):
        actual, steps = run(name, page, actions, dpr)
        expected, full_steps = run(name + '-full', page, actions, dpr, full=True)
        assert actual == expected, (name, 'copied pixels differ from full repaint')
        first, last = steps[0]['widget_paint'], steps[-1]['widget_paint']
        assert (last['scroll_blits'] > first['scroll_blits']) == reuse, (name, first, last)
        if reuse:
            full_first, full_last = full_steps[0]['widget_paint'], full_steps[-1]['widget_paint']
            assert last['paint_pixels'] - first['paint_pixels'] < full_last['paint_pixels'] - full_first['paint_pixels'], name
            previous = steps[-2]['widget_paint']
            assert (last['scroll_repaints'] == previous['scroll_repaints']) == last_reuse, (name, previous, last)
            if last_reuse:
                assert last['scroll_blits'] > previous['scroll_blits'], (name, previous, last)
        return steps

    paths = {
        'before': ['0,100'],
        'threshold': ['0,100', '0,140'],
        'overlap': ['0,140', '0,300'],
        'limit': ['0,400', '0,700', '0,830', '0,880', '0,930'],
        'reverse': ['0,180', '0,90'],
        'diagonal': ['0,160', '60,220', '120,270'],
        'horizontal': ['0,160', '60,160', '120,160'],
        'roundtrip': ['0,160', '0,0'],
    }
    for dpr in (1, 2):
        for case, positions in paths.items():
            compare(f'{case}-{dpr}', repo / 'test/sticky-scroll-reuse.html',
                    [('scroll', value) for value in positions], dpr, last_reuse=case != 'diagonal')
        compare(f'contexts-{dpr}', repo / 'test/sticky-contexts.html',
                [('scroll', '0,180'), ('scroll', '0,380'), ('scroll', '0,420')], dpr)
        compare(f'clipped-{dpr}', repo / 'test/sticky-contexts.html',
                [('scroll', '0,400'), ('scroll', '0,800'), ('scroll-element', 'inner-port,0,100'),
                 ('scroll', '0,820'), ('scroll', '0,840')], dpr)

    # The clip stays in document space while different header pixels move into it.
    page = root / 'same-clip.html'
    page.write_text('''<!doctype html><style>body{margin:0;height:3000px}
        .clip{position:absolute;top:0;left:20px;width:200px;height:500px;clip:rect(180px,200px,200px,0)}
        .before{height:100px}header{position:sticky;top:10px;height:100px;background:#579}
        p{margin:0;height:20px;background:#b65}p:nth-child(2n){background:#7a3}</style>
        <div class="clip"><div class="before"></div><header>
        <p>First</p><p>Second</p><p>Third</p><p>Fourth</p></header></div>''')
    compare('same-clip', page, [('scroll', '0,130'), ('scroll', '0,150')])

    # Budget limits must choose a full repaint before copying any pixels.
    page = root / 'large.html'
    page.write_text('''<!doctype html><style>body{margin:0;height:3000px}
        header{position:sticky;top:0;height:550px;background:#579}</style><header>Large</header>''')
    compare('large', page, [('scroll', '0,30')], reuse=False)
    page = root / 'fragmented.html'
    parts = ''.join(f'<div style="left:{i*18}px"><header></header></div>' for i in range(40))
    page.write_text('''<!doctype html><style>body{margin:0;height:3000px}
        div{position:absolute;top:80px;width:8px;height:1500px}
        header{position:sticky;top:10px;height:10px;background:#579}</style>''' + parts)
    compare('fragmented', page, [('scroll', '0,100')], reuse=False)

print('Sticky viewport reuse: native pixels, thresholds, containment, overlap, nested clips, DPR and budgets passed.')
