#!/usr/bin/env python3
"""Clones inserted into the rendered DOM must match equivalent static markup."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
prefix = '''<!doctype html><meta charset="utf-8"><title>Cloning</title>
<style>body{margin:0;font:20px/30px monospace}section{display:block;width:260px;margin:10px;background:#dfd}
span{color:#036}svg{display:block}</style>'''
section = '<section id="source" style="padding:7px"><span>Cloned text</span><svg width="40" height="20"><rect width="40" height="20" fill="blue"/></svg></section>'
clone = section.replace('id="source"', 'id="copy"')
script = '''function change(){
 const doc=document.cloneNode(true), source=doc.getElementById('source');
 const copy=source.cloneNode(true); copy.id='copy';
 if(copy.ownerDocument!==doc || copy.isConnected)throw Error('clone ownership');
 document.body.appendChild(copy);
 if(copy.ownerDocument!==document || !copy.isConnected)throw Error('insertion adoption');
 console.log('CLONE_INSERTED');
}'''
with tempfile.TemporaryDirectory(prefix='wisp-js-clone-') as tmp:
    root = Path(tmp)
    reference = None
    for case in ('reference', 'disabled', 'inline', 'timer'):
        page = root / (case + '.html')
        # Keep the script in all variants so cloning the document also traverses
        # a script element; its text and execution state must not be shared.
        markup = prefix + '<body>' + section + (clone if case == 'reference' else '')
        call = 'setTimeout(change,150)' if case == 'timer' else 'change()'
        markup += '<script>' + script + call + '</script>'
        page.write_text(markup)
        dest = root / case
        result = subprocess.run([str(binary), '-v', '--js_trace=1',
            '--enable_javascript=' + str(int(case in ('inline', 'timer'))),
            '--capture-dir', str(dest), '--capture-size', '400x250', '--capture-id', 'copy',
            '--capture-settle', '500', '--capture-timeout', '6000', str(page)],
            env=env, capture_output=True, text=True, timeout=12)
        log = result.stdout + result.stderr
        diagnostic = '\n'.join(line for line in log.splitlines() if '[JS ' in line or 'JavaScript ' in line or 'Console:' in line)
        assert result.returncode == 0 and 'runtime error:' not in log, (case, log[-8000:])
        metadata = json.loads((dest / 'capture.json').read_text())
        assert metadata['result'] == 'complete' and metadata['load_complete'], (case, metadata, diagnostic)
        assert 'JavaScript exception' not in log, (case, diagnostic)
        pixels = (dest / 'page.png').read_bytes()
        if case == 'reference':
            reference = pixels
        elif case == 'disabled':
            assert pixels != reference, case
        else:
            assert pixels == reference, (case, diagnostic)
            if 'Console: CLONE_INSERTED' in log:
                assert log.count('dom.clone ') == 2, diagnostic
                assert log.count('render.request ') == 1, diagnostic
                assert 'mode=' + ('full-restyle' if case == 'timer' else 'initial-build') in diagnostic, diagnostic
