#!/usr/bin/env python3
"""HTML objects are replaced boxes without natural dimensions or aspect ratio.

Expectations come from HTML's embedded-content rendering rules and CSS 2.2
10.3.2/4/6/8 and 10.6.2/5, not a reference browser's output:
https://html.spec.whatwg.org/multipage/rendering.html#embedded-content-rendering-rules
https://www.w3.org/TR/CSS22/visudet.html#inline-replaced-width
https://www.w3.org/TR/CSS22/visudet.html#inline-replaced-height
CSS aspect-ratio supplies a preferred ratio independently of natural dimensions:
https://www.w3.org/TR/css-sizing-4/#aspect-ratio
Grid auto margins suppress stretch before distributing free space:
https://www.w3.org/TR/css-grid-1/#auto-margins
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import quote

MODES = {
    "block": ("", "display:block"),
    "flow-root": ("", "display:flow-root"),
    "inline": ("", "display:inline"),
    "inline-block": ("", "display:inline-block"),
    "float": ("display:flow-root", "float:left"),
    "absolute": ("", "position:absolute;inset:10px 20px 30px 40px"),
    "flex": ("display:flex;align-items:flex-start", "flex:none"),
    # An auto margin prevents inline stretch without relying on justify-items
    # support. CSS Grid 10.2 sizes such an item before distributing free space.
    "grid": ("display:grid;grid-template-columns:600px;align-items:start", "margin-right:auto"),
}
SIZES = {
    "auto": ("", (300, 150)),
    "width": ("width:180px", (180, 150)),
    "height": ("height:70px", (300, 70)),
    "explicit": ("width:180px;height:70px", (180, 70)),
    "minimum": ("min-width:320px;min-height:180px", (320, 180)),
    "maximum": ("max-width:220px;max-height:90px", (220, 90)),
    "ratio": ("width:180px;aspect-ratio:auto 2", (180, 90)),
    "border-box": ("width:180px;height:70px;box-sizing:border-box;padding:5px;border:1px solid", (180, 70)),
    "overflow": ("", (300, 150)),
    "overflow-minimum": ("min-width:320px;min-height:180px", (320, 180)),
    "explicit-minimum": ("width:180px;height:70px;min-height:100px", (180, 100)),
}


def fixture():
    parts = ['<!doctype html><style>html,body{margin:0}body{font:16px/20px sans-serif}'
             '.case{position:relative;width:600px;height:350px;margin-bottom:10px}</style>'
             '<details open><summary id="summary">Embedded documents</summary>']
    for mode, (parent_style, object_style) in MODES.items():
        for name, (size_style, _) in SIZES.items():
            child = '<!doctype html><style>body{margin:0}</style><p>Embedded document</p>'
            if name.startswith("overflow"):
                child = '<!doctype html><style>body{margin:0}</style><div style="width:900px;height:500px">Overflow</div>'
            url = 'data:text/html,' + quote(child)
            key = mode + '-' + name
            parts.append(f'<div class="case" style="{parent_style}"><object id="{key}" data="{url}"'
                         f' style="{object_style};{size_style}">Fallback</object></div>')
    parts.append('</details>')
    return '\n'.join(parts)


binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-object-sizing-") as tmp:
    root = Path(tmp)
    page = root / 'objects.html'
    page.write_text(fixture())
    out = root / 'capture'
    args = [str(binary), '--capture-dir', str(out), '--capture-size', '800x600',
            '--capture-settle', '50', '--capture-timeout', '30000', '--enable_javascript=0',
            '--capture-resize', '900x700', '--capture-resize', '800x600',
            '--capture-click-id', 'summary', '--capture-click-id', 'summary']
    for mode in MODES:
        for name in SIZES:
            args += ['--capture-id', mode + '-' + name]
    process = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=40)
    assert process.returncode == 0 and 'runtime error:' not in process.stderr, process.stderr
    meta = json.loads((out / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], meta
    steps = json.loads((out / 'steps.json').read_text())
    assert len(steps) == 5, len(steps)
    failures = []
    for index, step in enumerate(steps):
        elements = {e['id']: e for e in step['elements']}
        assert step['active_resources'] == 0, (index, step['active_resources'])
        assert step['resource_count'] == steps[0]['resource_count'], (index, step['resource_count'])
        for mode in MODES:
            for name, (_, expected) in SIZES.items():
                key = mode + '-' + name
                boxes = elements[key]['boxes']
                if index == 3:
                    assert not boxes, (index, key)
                    continue
                assert boxes, (index, key, elements[key])
                rect = boxes[0]['border_box']
                actual = rect['width'], rect['height']
                if actual != expected:
                    failures.append(f'step {index}, {key}: expected {expected}, got {actual}')
    assert not failures, '\n'.join(failures)
print(f'Embedded documents: {len(MODES) * len(SIZES)} sizing cases passed across resize and disclosure changes.')
