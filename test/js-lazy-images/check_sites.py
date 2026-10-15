#!/usr/bin/env python3
"""Exercise preserved site markup with its real image loader and local assets.

The script leaves the static index.html baselines intact. Its generated variant
adds only lazySizes, not unrelated site scripts. Live-site checks remain separate.
Artifacts and a per-viewport native image-state report are kept for review.
"""
import argparse
from html.parser import HTMLParser
import json
import math
import os
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--output', type=Path, default=repo / 'out/js-lazy-images/site-validation')
parser.add_argument('--check-existing', action='store_true', help='Recheck saved native captures without opening Wisp')
args = parser.parse_args()
binary = args.binary.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')


class Images(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if tag == 'img' and 'lazyload' in attributes.get('class', '').split():
            self.ids.append(attributes['id'])


def capture(page, name, width, ids, scrolls):
    dest = output / name
    if not args.check_existing and dest.exists() and any(dest.iterdir()):
        raise RuntimeError(f'Choose a fresh --output directory; capture already exists: {dest}')
    command = [str(binary), '--enable_javascript=1', '--capture-dir', str(dest),
               '--capture-size', f'{width}x900', '--capture-settle', '500', '--capture-timeout', '180000']
    for id in ids:
        command += ['--capture-id', id]
    for y in scrolls:
        command += ['--capture-scroll', f'0,{y}']
    command.append(str(page))
    if not args.check_existing:
        with (output / f'{name}.log').open('w') as log:
            p = subprocess.run(command, env=env, stdout=log, stderr=log, timeout=195)
        assert p.returncode == 0, (name, 'See capture log', p.returncode)
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (name, meta)
    steps = (json.loads((dest / 'steps.json').read_text()) if scrolls else
             [dict(json.loads((dest / 'inspection.json').read_text()), scroll={'x': 0, 'y': 0})])
    return meta, steps


def summarize(step, width):
    loaded, visible, missing, failed = [], [], [], []
    top = step['scroll']['y']
    for element in step['elements']:
        state = element['state']
        source = state.get('current_src', '')
        real = source and not source.startswith('data:')
        # These snapshots localize their transparent 1x1 PNG placeholders too;
        # a successfully decoded local URL alone is not an article image.
        available = real and state['complete'] and state['natural_width'] > 1 and state['natural_height'] > 1
        if available:
            loaded.append(element['id'])
        elif real and state['complete'] and state['natural_width'] == 0:
            failed.append(element['id'])
        for box in element['boxes']:
            r = box['border_box']
            if (box['visible'] and r['width'] > 0 and r['height'] > 0 and r['x'] < width and
                    r['x'] + r['width'] > 0 and r['y'] < top + 900 and r['y'] + r['height'] > top):
                visible.append(element['id'])
                if not available:
                    missing.append({'id': element['id'], 'source': source, 'state': state})
                break
    return {'scroll': top, 'loaded': len(loaded), 'visible': len(visible), 'missing': missing,
            'failed': failed, 'active_resources': step['active_resources']}


report = []
for site in ('tion', 'bihon'):
    source = repo / f'sites/{site}-2026-09-28/index.html'
    page = source.read_text().replace('<head>', f'<head><base href="{source.as_uri()}">', 1)
    page += f'<script src="{(repo / "test/js-lazy-images/vendor/lazysizes-4.0.3.min.js").as_uri()}"></script>'
    variant = output / f'{site}.html'
    variant.write_text(page)
    inventory = Images()
    inventory.feed(page)
    for width in (1280, 390):
        name = f'{site}-{width}'
        meta, _ = capture(variant, name + '-initial', width, inventory.ids, [])
        # Traverse overlapping viewports. Extra steps cover shifts as image data
        # arrives; the final jump confirms that the document's end was reached.
        end = math.ceil(meta['document_extent']['height'] / 750) * 750 + 4500
        meta, steps = capture(variant, name + '-scroll', width, inventory.ids,
                              [*range(750, end, 750), 1000000])
        summary = [summarize(step, width) for step in steps]
        entry = {'site': site, 'width': width, 'lazy_images': len(inventory.ids),
                 'load_complete': meta['load_complete'], 'extent': meta['document_extent'], 'steps': summary}
        report.append(entry)
        (output / 'report.json').write_text(json.dumps(report, indent=2))
        missing = [(s['scroll'], s['missing']) for s in summary if s['missing']]
        print(name, 'loaded', summary[0]['loaded'], '->', summary[-1]['loaded'],
              'missing visible:', len(missing), 'complete:', meta['load_complete'], flush=True)
        assert all(s['active_resources'] == 0 for s in summary), name
        assert not missing, (name, missing)
