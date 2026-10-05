#!/usr/bin/env python3
"""HTML object image-handler fallback for sources with readable headers but no pixels.

https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
# A valid RIFF/VP8L header describing 1x1 pixels, with no encoded pixel data.
# WebPGetInfo succeeds; actual decoding fails.
broken = "data:image/webp;base64,UklGRhIAAABXRUJQVlA4TAUAAAAvAAAAAAA="
# JPEG SOF/SOS dimensions are readable, but the required quantization table is
# absent. jpeg_read_header succeeds; jpeg_start_decompress fails.
broken_jpeg = "data:image/jpeg;base64,/9j/wAALCAABAAEBAREA/9oACAEBAAA/AP/Z"
valid = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"
page = ('<!doctype html><style>object{display:block}object::before{content:"fallback-pseudo"}'
        '#background{width:10px;height:10px;background-image:url(' + broken + ')}</style>'
        '<details open id="panel"><summary id="summary">Objects</summary>' +
        ''.join(f'<object id="object-{i}" data="{broken}"><p id="fallback-{i}">broken-image fallback</p>'
                '</object>' for i in range(2)) +
        f'<img id="image" src="{broken}"><div id="background"></div>'
        f'<object id="valid" data="{valid}"><p id="forbidden">valid-image fallback</p></object></details>')
ids = ('panel', 'object-0', 'object-1', 'fallback-0', 'fallback-1', 'forbidden', 'valid')
actions = [('click-id', 'summary'), ('find', 'broken-image'), ('find', 'fallback-pseudo'),
           ('resize', '600x400'), ('click-id', 'summary'), ('click-id', 'summary')]

with tempfile.TemporaryDirectory(prefix="wisp-object-decode-") as tmp:
    root = Path(tmp)
    fixture = root / 'page.html'
    cases = ((kind, source, mode, size) for kind, source in (('webp', broken), ('jpeg', broken_jpeg))
             for mode, size in (('speculative', 33554432), ('deferred', 0)))
    for kind, source, mode, cache_size in cases:
        fixture.write_text(page.replace(broken, source))
        mode = kind + '-' + mode
        out = root / mode
        args = [str(binary), '--capture-dir', str(out), '--capture-size', '700x500',
                '--capture-settle', '80', '--capture-timeout', '15000', '--enable_javascript=0',
                f'--image_cache_size={cache_size}']
        for key in ids:
            args += ['--capture-id', key]
        for kind, value in actions:
            args += ['--capture-' + kind, value]
        result = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0 and 'runtime error:' not in result.stderr, result.stderr
        assert 'UNDERFLOW' not in result.stderr, result.stderr
        meta = json.loads((out / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], (mode, meta)
        steps = json.loads((out / 'steps.json').read_text())
        assert len(steps) == len(actions) + 1, (mode, len(steps))
        for index, step in enumerate(steps):
            elements = {e['id']: e for e in step['elements']}
            opened = index not in (1, 5)
            assert elements['panel']['state']['open'] == opened, (mode, index)
            for i in range(2):
                assert bool(elements[f'fallback-{i}']['boxes']) == opened, (mode, index, i)
            assert not elements['forbidden']['boxes'], (mode, index)
            assert step['active_resources'] == 0, (mode, index, step['active_resources'])
            # Two objects, the image's preload + layout request, background,
            # and the successful object. Rebuilding must not add requests.
            assert step['resource_count'] == steps[0]['resource_count'] == 6, (mode, index, step['resource_count'])
            if index == 2:
                assert elements['fallback-0']['boxes'][0]['highlighted_fragments'] == 1, (mode, index)
            if index == 3:
                assert elements['object-0']['boxes'][0]['highlighted_before_fragments'] == 1, (mode, index)
print('Object decode failure: speculative/deferred decode, shared consumers, search and repeated reveal passed.')
