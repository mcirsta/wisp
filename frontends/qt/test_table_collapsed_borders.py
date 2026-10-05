#!/usr/bin/env python3
"""CSS 2.2 17.6.2: centered shared borders, conflict resolution and paint segments.

Expected dimensions follow 32x20 content blocks plus used half borders. Odd
widths use Wisp's consistent integer rounding; no browser-specific baselines.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

# Table width/height and cell rectangles relative to its outer border box.
CASES = {
    'even': ((70, 46), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 22), 'c': (1, 23, 34, 22)}),
    'odd': ((73, 49), {'a': (1, 1, 35, 23), 'b': (36, 1, 35, 23), 'c': (1, 24, 35, 23)}),
    'padding': ((70, 24), {'a': (1, 1, 34, 22)}),
    'override': ((70, 24), {'a': (1, 1, 34, 22)}),
    'wide': ((74, 24), {'a': (1, 1, 36, 22), 'b': (37, 1, 36, 22)}),
    'hidden': ((68, 24), {'a': (1, 1, 33, 22), 'b': (34, 1, 33, 22)}),
    'none': ((72, 24), {'a': (1, 1, 35, 22), 'b': (36, 1, 35, 22)}),
    'style': ((74, 24), {'a': (1, 1, 36, 22), 'b': (37, 1, 36, 22)}),
    'width': ((76, 24), {'a': (1, 1, 37, 22), 'b': (38, 1, 37, 22)}),
    'tie': ((70, 24), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 22)}),
    'origin': ((44, 32), {'a': (3, 3, 38, 26)}),
    'row': ((44, 32), {'a': (3, 3, 38, 26)}),
    'group': ((44, 32), {'a': (3, 3, 38, 26)}),
    'table': ((44, 32), {'a': (3, 3, 38, 26)}),
    'colspan': ((70, 50), {'a': (1, 1, 68, 24), 'b': (1, 25, 34, 24), 'c': (35, 25, 34, 24)}),
    'rowspan': ((74, 50), {'a': (1, 1, 36, 48), 'b': (37, 1, 36, 24), 'c': (37, 25, 36, 24)}),
    'groups': ((36, 50), {'a': (1, 1, 34, 24), 'b': (1, 25, 34, 24)}),
    'spill': ((44, 46), {'a': (1, 1, 42, 22), 'b': (1, 23, 42, 22)}),
    'separate': ((72, 24), {'a': (0, 0, 36, 24), 'b': (36, 0, 36, 24)}),
    'toggle': ((70, 24), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 22)}),
    'model': ((70, 24), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 22)}),
    'visible': ((70, 24), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 22)}),
    'nested': ((52, 40), {'a': (1, 1, 50, 38)}),
    'fraction': ((72, 24), {'a': (1, 1, 35, 22), 'b': (36, 1, 35, 22)}),
    'borderbox': ((80, 40), {'a': (5, 5, 70, 30)}),
    'span-tie': ((70, 46), {'a': (1, 1, 34, 22), 'b': (35, 1, 34, 44), 'c': (1, 23, 34, 22)}),
    'inner': ((40, 28), {'a': (2, 2, 36, 24)}),
}
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)
BLUE = (0, 0, 255)
GREEN = (0, 255, 0)
MAGENTA = (255, 0, 255)
YELLOW = (255, 255, 204)
# Sample away from junctions and dashed endpoints. Double borders have a gap.
PIXELS = {
    'even': [(0, 10, BLACK), (1, 10, BLACK), (2, 10, WHITE), (34, 10, BLACK),
             (35, 10, BLACK), (36, 10, WHITE), (10, 22, BLACK), (10, 23, BLACK)],
    'odd': [(0, 10, BLACK), (2, 10, BLACK), (3, 10, WHITE), (35, 10, BLACK), (37, 10, BLACK),
            (38, 10, WHITE)],
    'wide': [(34, 10, RED), (39, 10, RED), (40, 10, WHITE)],
    'hidden': [(33, 10, WHITE), (34, 10, WHITE)],
    'none': [(34, 10, BLUE), (37, 10, BLUE), (38, 10, WHITE)],
    'style': [(34, 10, RED), (35, 10, RED), (36, 10, YELLOW), (37, 10, YELLOW), (38, 10, RED)],
    'width': [(34, 10, BLUE), (41, 10, BLUE), (42, 10, WHITE)],
    'tie': [(34, 10, RED)],
    'origin': [(0, 10, RED), (10, 0, RED)],
    'row': [(0, 10, MAGENTA), (10, 0, MAGENTA)],
    'group': [(0, 10, GREEN), (10, 0, GREEN)],
    'table': [(0, 10, BLUE), (10, 0, BLUE)],
    'colspan': [(10, 25, RED), (50, 25, GREEN), (35, 10, YELLOW)],
    'rowspan': [(37, 10, BLUE), (37, 35, GREEN), (50, 25, RED), (10, 25, YELLOW)],
    'groups': [(10, 25, RED)],
    'spill': [(-4, 30, RED), (47, 30, RED), (-5, 30, WHITE), (48, 30, WHITE)],
    'visible': [(0, 10, WHITE), (34, 10, WHITE), (69, 10, BLACK)],
    'fraction': [(34, 10, BLUE), (37, 10, BLUE)],
    'borderbox': [(0, 20, RED), (9, 20, RED), (10, 20, WHITE)],
    'span-tie': [(34, 10, BLUE), (34, 35, GREEN)],
    'inner': [(0, 10, BLUE), (3, 10, BLUE), (4, 10, WHITE)],
}


def main(zoom):
    binary, magick = Path(sys.argv[1]).resolve(), sys.argv[2]
    fixture = Path(__file__).resolve().parents[2] / 'test/table-collapsed-borders.html'
    ids = re.findall(r' id="([^"]+)"', fixture.read_text())
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1', QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
    width, height = int(800 * zoom), int(1500 * zoom)
    with tempfile.TemporaryDirectory(prefix='wisp-table-borders-') as tmp:
        dest = Path(tmp) / 'capture'
        args = [str(binary), '--capture-dir', str(dest), '--capture-size', f'{width}x{height}',
                '--capture-settle', '100', '--capture-timeout', '15000', '--enable_javascript=0',
                f'--scale={int(100 * zoom)}']
        for key in ids:
            args += ['--capture-id', key]
        args += ['--capture-resize', f'{int(650 * zoom)}x{height}', '--capture-resize', f'{width}x{height}',
                 '--capture-click-id', 'activate', '--capture-click-id', 'activate', fixture.as_uri()]
        run = subprocess.run(args, env=env, capture_output=True, text=True, timeout=35)
        assert run.returncode == 0 and 'runtime error:' not in run.stderr, run.stderr
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], meta
        steps = json.loads((dest / 'steps.json').read_text())
        assert len(steps) == 5, len(steps)
        for index, step in enumerate(steps):
            elements = {e['id']: e for e in step['elements']}
            checked = index == 3
            assert elements['switch']['state']['checked'] == checked, (index, elements['switch'])
            for name, (size, cells) in CASES.items():
                if checked and name == 'toggle':
                    size, cells = (82, 32), {'a': (3, 3, 38, 26), 'b': (41, 3, 38, 26)}
                if checked and name == 'model':
                    size, cells = CASES['separate']
                assert elements[name].get('boxes'), (index, name, elements[name])
                origin = elements[name]['boxes'][0]['border_box']
                assert (origin['width'], origin['height']) == size, (index, name, origin, size)
                for suffix, expected in cells.items():
                    key = name + '-' + suffix
                    assert elements[key].get('boxes'), (index, key, elements[key])
                    box = elements[key]['boxes'][0]['border_box']
                    actual = (box['x'] - origin['x'], box['y'] - origin['y'], box['width'], box['height'])
                    assert actual == expected, (index, key, actual, expected)
        # The last snapshot is the round trip back to the original border model.
        pixels = subprocess.run([magick, str(dest / 'page.png'), '-depth', '8', 'RGB:-'],
                                check=True, capture_output=True).stdout
        assert len(pixels) == width * height * 3
        elements = {e['id']: e for e in steps[-1]['elements']}
        for name, samples in PIXELS.items():
            origin = elements[name]['boxes'][0]['border_box']
            for dx, dy, expected in samples:
                x, y = int((origin['x'] + dx + .5) * zoom), int((origin['y'] + dy + .5) * zoom)
                offset = (y * width + x) * 3
                actual = tuple(pixels[offset:offset + 3])
                assert actual == expected, (zoom, name, dx, dy, actual, expected)
    print(f'Collapsed borders: {len(CASES)} geometry cases, {sum(map(len, PIXELS.values()))} pixels, '
          f'resize and native restyle round trips at {zoom * 100:g}%.')


if __name__ == '__main__':
    main(1)
    main(1.5)
