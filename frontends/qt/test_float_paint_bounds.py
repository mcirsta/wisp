#!/usr/bin/env python3
"""CSS 2.2 E.2/11.1.1: floats paint beyond empty lines but respect real clips.

Run under Xvfb: native screenshots preserve stale/erased damage that QWidget.grab
would repaint. Check expected colors as well as scroll-reuse/full-paint equality;
two equally blank screenshots must not pass.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


BLUE = (34, 102, 204)
GREEN = (102, 187, 136)
WHITE = (255, 255, 255)
WIDTH, HEIGHT = 640, 480
CASES = [
    ('left', '', 1000, 0),
    ('right', '#column{float:right}', 1000, 0),
    ('nested-offset', '#wrapper{margin:29px 0 0 31px;padding:11px 0 0 13px}', 1000, 0),
    ('nested-float', '#wrapper{float:left;width:400px}', 1000, 0),
    ('zero-height-float', '#column{height:0}', 0, 0),
    ('relative-float', '#column{position:relative;left:11px;top:13px}', 1000, 0),
    ('absolute-container', '#stage{position:absolute;left:20px;top:95px;margin:0}', 1000, 0),
    ('clipped', '#stage{height:350px;overflow:hidden}', 1000, 0),
    ('scrollport', '#stage{height:350px;overflow:auto}', 1000, 450),
]


def main():
    binary = Path(sys.argv[1]).resolve()
    magick = sys.argv[2]
    fixture = Path(__file__).resolve().parents[2] / 'test/float-paint-bounds.html'
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR='1',
               QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    with tempfile.TemporaryDirectory(prefix='wisp-float-bounds-') as tmp:
        root = Path(tmp)

        def run(name, page, actions, full=False):
            dest = root / name
            args = [str(binary), '--capture-dir', str(dest), '--capture-size', f'{WIDTH}x{HEIGHT}',
                    '--capture-screen', '--capture-settle', '50', '--capture-timeout', '8000']
            if full:
                args += ['--capture-no-scroll-blit']
            for eid in ('stage', 'wrapper', 'column', 'first', 'second'):
                args += ['--capture-id', eid]
            for kind, value in actions:
                args += ['--capture-' + kind, value]
            result = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, (name, result.stderr[-4000:])
            assert 'runtime error:' not in result.stderr, (name, result.stderr)
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (name, meta)
            steps = json.loads((dest / 'steps.json').read_text())
            boxes = {}
            for e in steps[-1]['elements']:
                assert e['status'] == 'ok' and e.get('boxes'), (name, e)
                boxes[e['id']] = e['boxes'][0]['border_box']
            pixels = subprocess.run([magick, str(dest / 'page.png'), '-depth', '8', 'RGB:-'],
                                    check=True, capture_output=True).stdout
            assert len(pixels) == WIDTH * HEIGHT * 3, name
            return pixels, boxes, meta, steps

        def color_at(pixels, x, y, expected, name):
            assert 0 <= x < WIDTH and 0 <= y < HEIGHT, (name, x, y)
            offset = (y * WIDTH + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            assert actual == expected, (name, (x, y), actual, expected)

        for name, style, column_height, inner_scroll in CASES:
            page = root / (name + '.html')
            page.write_text(fixture.read_text().replace('/* CASE_STYLE */', style))
            prefix = [('scroll-element', f'stage,0,{inner_scroll}')] if inner_scroll else []
            # Damage can miss the anonymous wrapper even before it leaves the
            # viewport. Paint the strip containing the sampled first band.
            pixels, boxes, _, _ = run(name + '-damage', page, [*prefix, ('repaint', '0,280,640,60')])
            x = boxes['column']['x'] + 60
            color_at(pixels, x, 300, GREEN if inner_scroll else BLUE, name + '-damage')
            wrapper_height = 11 if name == 'nested-offset' else 17  # Padding only, content remains zero.
            assert boxes['wrapper']['height'] == wrapper_height, (name, boxes['wrapper'])
            assert boxes['column']['height'] == column_height, (name, boxes['column'])

            actions = [*prefix, ('scroll', '0,120'), ('scroll', '0,240'),
                       ('repaint', '0,60,640,60;0,270,640,60')]
            actual, boxes, meta, steps = run(name + '-scroll', page, actions)
            reference, _, full_meta, _ = run(name + '-full', page, actions, full=True)
            assert actual == reference, (name, 'scroll reuse differs from full repaint')
            assert steps[-1]['scroll']['y'] == 240, (name, steps[-1]['scroll'])
            color_at(actual, x, 100, GREEN if inner_scroll else BLUE, name + '-scroll')
            if name in ('clipped', 'scrollport'):
                color_at(actual, x, 300, WHITE, name + '-outside-clip')
                assert meta['document_extent']['height'] == 1045, (name, meta['document_extent'])
            assert meta['widget_paint']['scroll_blits'] > 0, (name, meta['widget_paint'])
            assert meta['widget_paint']['paint_pixels'] < full_meta['widget_paint']['paint_pixels'], name
            if name not in ('clipped', 'scrollport'):
                pixels, _, _, _ = run(name + '-lower', page, [('scroll', '0,420')], full=True)
                color_at(pixels, x, 300, GREEN, name + '-lower-band')
    print(f'Float bounds: {len(CASES)} damage, scrolling, clipping and sizing cases passed.')


if __name__ == '__main__':
    main()
