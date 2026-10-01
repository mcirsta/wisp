#!/usr/bin/env python3
"""Compare actual X11 viewport pixels after scrolling with full repaints.

QWidget.grab() repaints and would conceal a stale-pixel blit bug. Run this test
under xvfb-run and use the capture tool's native screen grab instead.
"""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

binary = Path(sys.argv[1]).resolve()


def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))


pixels = b''.join(b'\0' + bytes([x % 256, y % 256, (x + y) % 256, 160]) * 320
                  for y, x in enumerate(range(180)))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!IIBBBBB', 320, 180, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')
uri = 'data:image/png;base64,' + base64.b64encode(png).decode()

with tempfile.TemporaryDirectory(prefix='wisp-scroll-reuse-') as temp:
    root = Path(temp)
    (root / 'child.html').write_text('<!doctype html><p>Child frame</p>')
    rows = ''.join(f'<article><h2>Reading row {i}</h2><img src="{uri}">'
                   '<p>Text beside resized images and patterned backgrounds.</p></article>' for i in range(18))
    common = ('<!doctype html><style>body{margin:0;background:white;font:16px sans-serif;min-width:1200px}'
              'article{height:210px;border-bottom:3px solid #369;background:#f8f0d0}'
              'img{width:160px;height:90px}h2{margin:0}header{height:30px;background:#c36;width:600px}'
              f'.tile{{background-image:url("{uri}");background-size:80px 45px}}'
              '</style>')
    cases = {
        'ordinary': '<header>Ordinary</header>',
        'sticky': '<header style="position:sticky;top:0">Sticky</header>',
        'fixed': '<header style="position:fixed;top:0;left:0">Fixed</header>',
        'fixed-background': f'<style>body{{background-image:url("{uri}");background-attachment:fixed}}</style>',
        'layered-fixed-background': f'<style>body{{background-image:url("{uri}"),url("{uri}");background-attachment:scroll,fixed}}</style>',
        'iframe': '<iframe src="child.html" style="width:200px;height:60px"></iframe>',
        'tiled': '<header class="tile">Tiled background</header>',
    }

    def run(name, page, *, reference=False, zoom=100, dpr=1, actions=None, no_cache=False):
        dest = root / name
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', '640x480', '--capture-screen',
                '--capture-settle', '50', '--capture-timeout', '10000', f'--scale={zoom}']
        if reference:
            args += ['--capture-no-scroll-blit']
        if no_cache:
            args += ['--capture-bitmap-cache-bytes', '0']
        for kind, value in (actions or [('scroll', '0,90'), ('scroll', '0,180'), ('scroll', '70,180'),
                                       ('scroll', '140,270'), ('scroll', '0,180'), ('scroll', '0,90')]):
            args += ['--capture-' + kind, value]
        p = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=20)
        assert p.returncode == 0, (name, p.returncode, p.stderr[-4000:])
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], (name, meta)
        assert meta['image_pixels'] == {'width': int(640 * dpr), 'height': int(480 * dpr)}, meta
        meta['steps'] = json.loads((dest / 'steps.json').read_text())
        return (dest / 'page.png').read_bytes(), meta

    for case, prefix in cases.items():
        page = root / f'{case}.html'
        page.write_text(common + prefix + rows)
        actual, stats = run(case, page)
        reference, full = run(case + '-full', page, reference=True, no_cache=True)
        assert actual == reference, (case, 'cached/scrolled pixels differ from full repaint')
        if case in ('ordinary', 'tiled'):
            assert stats['widget_paint']['scroll_blits'] > 0, (case, stats)
            assert stats['widget_paint']['paint_pixels'] < full['widget_paint']['paint_pixels'], (stats, full)
        else:
            assert stats['widget_paint']['scroll_blits'] == 0, (case, stats)
        assert stats['bitmap_cache']['hits'] > 0, (case, stats)

    ordinary = root / 'ordinary.html'
    for zoom, dpr, blits in ((150, 1, False), (100, 2, True), (100, 1.25, False)):
        # Odd deltas at fractional device scaling must repaint to avoid moving
        # rasterized glyphs by a rounded physical-pixel distance.
        actions = [('scroll', '0,91'), ('scroll', '0,182'), ('scroll', '71,182')]
        name = f'zoom-{zoom}-dpr-{dpr}'
        actual, stats = run(name, ordinary, zoom=zoom, dpr=dpr, actions=actions)
        reference, _ = run(name + '-full', ordinary, reference=True, zoom=zoom, dpr=dpr, actions=actions)
        assert actual == reference, name
        assert bool(stats['widget_paint']['scroll_blits']) == blits, (name, stats)

    actions = [('scroll', '0,2000'), ('scroll', '0,2090'), ('resize', '600x400'),
               ('scroll', '0,2180'), ('resize', '640x480')]
    actual, _ = run('large-jump', ordinary, actions=actions)
    reference, _ = run('large-jump-full', ordinary, reference=True, actions=actions)
    assert actual == reference

    # A style update must recompute eligibility in both directions.
    page = root / 'restyle.html'
    page.write_text(common + '<style>#toggle:checked~header{position:fixed;top:40px}</style>'
                    '<input id="toggle" type="checkbox"><label for="toggle">Fix header</label>'
                    '<header>Switching header</header>' + rows)
    actions = [('scroll', '0,90'), ('click-label', 'toggle'), ('scroll', '0,180')]
    for fixed in (True, False):
        name = 'restyle-fixed' if fixed else 'restyle-ordinary'
        actual, stats = run(name, page, actions=actions)
        reference, _ = run(name + '-full', page, reference=True, actions=actions)
        assert actual == reference, name
        last, previous = (step['widget_paint'] for step in stats['steps'][-2:][::-1])
        assert (last['scroll_blits'] > previous['scroll_blits']) == (not fixed), (name, last, previous)
        actions += [('click-label', 'toggle'), ('scroll', '0,270')]

print('Qt viewport reuse: displayed pixels, fallback cases, bitmap cache, zoom and device scaling passed.')
