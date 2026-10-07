#!/usr/bin/env python3
"""Real DOM edits must change boxes and pixels, with attributable JS logging."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
prefix = '''<!doctype html><meta charset="utf-8"><title>DOM mutation fixture</title>
<style>body { margin: 0; font: 20px/30px monospace; }
.old { width: 120px; height: 40px; background: red; }
.new { width: 240px; height: 80px; background: #20a060; color: white; }
#reveal[hidden] { display: none; } #reveal { height: 35px; background: #ffee66; }
</style>'''
initial = '<div id="target" class="old">Before <span>child</span></div><div id="reveal" hidden>Revealed</div>'
expected = '<div id="target" class="new">Changed ș😀</div><div id="reveal">Revealed</div>'
mutation = '''const target = document.getElementById('target');
if (target !== document.body.firstChild || target !== document.getElementById('target')) throw Error('identity');
target.className = 'new';
Promise.resolve().then(() => {
    target.textContent = 'Changed ș😀';
    document.getElementById('reveal').removeAttribute('hidden');
    console.log('DOM_CHANGED');
});'''


def run(root, name, body, js=True):
    page = root / (name + '.html')
    page.write_text(prefix + body)
    dest = root / name
    result = subprocess.run([str(binary), '-v', '--js_trace=1', '--script_timeout=1',
                             f'--enable_javascript={int(js)}', '--capture-dir', str(dest),
                             '--capture-size', '500x300', '--capture-id', 'target', '--capture-id', 'reveal',
                             '--capture-settle', '600', '--capture-timeout', '6000', str(page)],
                            env=env, capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    diagnostic = '\n'.join(line for line in log.splitlines() if '[JS ' in line or 'JavaScript ' in line or 'Console:' in line)
    assert result.returncode == 0 and 'runtime error:' not in log, (name, diagnostic, log[-8000:])
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (name, meta, diagnostic)
    elements = json.loads((dest / 'inspection.json').read_text())['elements']
    assert all(e['status'] in ('ok', 'no_box') for e in elements), (name, elements)
    return dest, elements, diagnostic


with tempfile.TemporaryDirectory(prefix='wisp-js-dom-') as tmp:
    root = Path(tmp)
    reference, reference_elements, _ = run(root, 'reference', expected, False)
    initial_capture, _, _ = run(root, 'disabled', initial + '<script>' + mutation + '</script>', False)
    assert (initial_capture / 'page.png').read_bytes() != (reference / 'page.png').read_bytes()
    for name, script in (
            ('inline', mutation),
            ('timer', 'setTimeout(() => {' + mutation + '}, 150);'),
            ('exception', mutation + "throw Error('Expected after mutation');"),
            ('interrupted', "setTimeout(() => {" + mutation.replace('Promise.resolve().then(() => {', '{').replace('});', '}') +
             'while (true) {} }, 150);')):
        dest, elements, log = run(root, name, initial + '<script>' + script + '</script>')
        assert 'Console: DOM_CHANGED' in log, (name, log)
        assert (dest / 'page.png').read_bytes() == (reference / 'page.png').read_bytes(), (name, elements, log)
        for actual, expected_element in zip(elements, reference_elements):
            assert actual['boxes'] and expected_element['boxes'], (name, actual, expected_element)
            assert actual['boxes'][0]['border_box'] == expected_element['boxes'][0]['border_box'], (name, actual)
        changes = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] dom.mutation (.*)', log)
        requests = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] render.request (.*)', log)
        assert len(changes) == 3 and len(requests) == 1, (name, log)
        assert all(doc == requests[0][0] and entry == requests[0][1] for doc, entry, _ in changes), (name, log)
        assert 'changes=3' in requests[0][2], (name, log)
        assert re.search(r'dom.node node=\d+ type=1 name=DIV id=target', log), (name, log)
        assert 'operation=removeAttribute attribute=hidden result=0' in log, (name, log)
        assert ('mode=initial-build' if name in ('inline', 'exception') else 'mode=full-restyle') in requests[0][2], (name, log)
        if name == 'interrupted':
            assert 'JavaScript execution limit reached' in log and 'end result=interrupted' in log, log

print('JS DOM edits, coalesced rendering, interruption recovery, geometry, pixels and trace attribution pass.')
