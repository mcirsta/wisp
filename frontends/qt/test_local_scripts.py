#!/usr/bin/env python3
"""Saved pages must execute local scripts with real MIME types and paint edits."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
css = '''body { margin: 0; font: 20px/30px monospace; }
#target { width: 240px; height: 80px; background: red; }
#target.ready { background: #20a060; }
#async { width: 240px; height: 30px; }
'''
legacy_css = '#target { color: white; }'
markup = '<div id="target">Before</div><div id="async" hidden>Async loaded</div>'
expected = '<div id="target" class="ready">Changed ș😀</div><div id="async">Async loaded</div>'
header = '<!doctype html><meta charset="utf-8"><title>Local scripts</title>'
scripts = '''<script>
    var steps = [];
    document.addEventListener('DOMContentLoaded', () => {
        if (steps.join() !== 'first,job,inline,upper,defer') throw Error('order: ' + steps);
        console.log('LOCAL_DCL_OK');
    });
    window.addEventListener('load', () => {
        if (!asynchronous) throw Error('async not loaded');
        console.log('LOCAL_LOAD_OK');
    });
</script>
<script src="res ș/first.js"></script>
<script>steps.push('inline');</script>
<script src="res ș/upper.JS"></script>
<script src="res ș/missing.js"></script>
<script src="res ș/rejected.html"></script>
<script defer src="res ș/deferred.js"></script>
<!-- The .mjs filename is served as JavaScript; this is a classic script. -->
<script async src="res ș/asynchronous.mjs"></script>
<script type="module" src="res ș/unsupported.js"></script>'''


def run(root, name, contents, enabled=True, suffix='.html'):
    page = root / (name + suffix)
    page.write_text(contents, encoding='utf-8')
    dest = root / (name + '-capture')
    result = subprocess.run([
        str(binary), '-v', '--js_trace=1', f'--enable_javascript={int(enabled)}',
        '--capture-dir', str(dest), '--capture-size', '400x200',
        '--capture-id', 'target', '--capture-id', 'async',
        '--capture-settle', '50', '--capture-timeout', '6000', str(page),
    ], env=env, capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    diagnostic = '\n'.join(line for line in log.splitlines()
                           if 'JavaScript ' in line or 'Console: ' in line or 'Sanitizer' in line)
    assert result.returncode == 0 and 'runtime error:' not in log, (name, diagnostic, log[-6000:])
    assert 'JavaScript exception' not in log and 'Console: UNEXPECTED' not in log, (name, diagnostic)
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (name, meta, diagnostic)
    info = json.loads((dest / 'inspection.json').read_text())
    assert info['elements'][0]['status'] == 'ok' and info['elements'][0]['boxes'], (name, info)
    return (dest / 'page.png').read_bytes(), log


with tempfile.TemporaryDirectory(prefix='wisp-local-scripts-') as tmp:
    root = Path(tmp)
    assets = root / 'res ș'
    assets.mkdir()
    for filename, body in {
        'site.css': css,
        'legacy.f79': legacy_css,
        'first.js': """steps.push('first');
            document.getElementById('target').className = 'ready';
            queueMicrotask(() => steps.push('job'));""",
        'upper.JS': "steps.push('upper');",
        'deferred.js': """steps.push('defer');
            document.getElementById('target').textContent = 'Changed ș😀';""",
        'asynchronous.mjs': """var asynchronous = true;
            document.getElementById('async').removeAttribute('hidden');""",
        'rejected.html': "console.log('UNEXPECTED_HTML_SCRIPT');",
        'unsupported.js': "console.log('UNEXPECTED_MODULE_SCRIPT');",
    }.items():
        (assets / filename).write_text(body, encoding='utf-8')

    # Inline reference CSS prevents a shared stylesheet-fetch failure from
    # making both captures wrong in the same way.
    style = '<style>' + css + legacy_css + '</style>'
    reference, _ = run(root, 'reference', header + style + expected, False)
    disabled_reference, _ = run(root, 'disabled-reference', header + style + markup, False)
    assert reference != disabled_reference, 'fixture must visibly change'
    page = (header + '<link rel="stylesheet" href="res ș/site.css">'
            '<link rel="stylesheet" href="res ș/legacy.f79">' + markup + scripts)
    # An extensionless HTML page still loads through content-based MIME
    # detection; the old universal text/html fallback is not needed.
    for name, enabled, suffix in (('enabled', True, '.html'), ('extensionless', True, ''),
                                  ('disabled', False, '.html')):
        pixels, log = run(root, name, page, enabled, suffix)
        assert pixels == (reference if enabled else disabled_reference), name
        assert log.count('Console: LOCAL_DCL_OK') == int(enabled), (name, log[-6000:])
        assert log.count('Console: LOCAL_LOAD_OK') == int(enabled), (name, log[-6000:])
        assert ('operation=removeAttribute attribute=hidden result=0' in log) == enabled, name

print('Local script MIME types, CSS, ordering, lifecycle, DOM edits and disabled JS pass.')
