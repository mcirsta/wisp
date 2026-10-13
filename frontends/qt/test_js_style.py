#!/usr/bin/env python3
"""CSSOM edits compile to the same pixels as static, specification-derived CSS."""
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
prefix = '''<!doctype html><meta charset="utf-8"><title>Inline style</title>
<style>body{margin:0;font:20px/30px monospace}main{display:flow-root;width:320px;--m:11px 13px 17px 19px}
#target{background:red;width:100px;height:20px;margin-bottom:23px!important}
#child{background:blue;width:40px;height:20px}svg{display:block}</style>'''
initial = '<main><div id="target">Text</div><div id="child"></div><svg width="50" height="25" id="svg"><rect width="50" height="25" fill="blue"/></svg></main>'
expected = initial.replace('id="target"', 'id="target" style="margin:11px 13px 17px 19px!important;margin-top:7px!important;width:200px;height:40px;background-color:green;opacity:0.5"')
expected = expected.replace('id="child"', 'id="child" style="margin-left:19px"').replace('id="svg"', 'id="svg" style="opacity:0.5"')
script = '''const e=document.getElementById('target'),s=e.style;
function change(){
  s.cssText='margin:var(--m)!important'; s.setProperty('margin-top','7px','important');
  if(s.margin!=='' || s.length!==4)throw Error('pending state');
  s.width='200px'; s.height='40px'; s.backgroundColor='green'; s.opacity='0.5';
  const c=document.getElementById('child');c.style.margin='var(--m)';c.style.removeProperty('margin-top');
  c.style.removeProperty('margin-right');c.style.removeProperty('margin-bottom');
  document.getElementById('svg').style.opacity='0.5';
  const before=s.cssText;s.width='nonsense';s.setProperty('width','100px','invalid');s.removeProperty('unknown');
  if(s.cssText!==before)throw Error('invalid edit');
  console.log('STYLE_CHANGED');
}
'''
with tempfile.TemporaryDirectory(prefix='wisp-js-style-') as tmp:
    root = Path(tmp)
    reference = None
    for case in ('reference', 'disabled', 'inline', 'timer', 'replacement'):
        page = root / (case + '.html')
        markup = prefix + (expected if case == 'reference' else initial)
        if case != 'reference':
            call = 'setTimeout(change,150)' if case == 'timer' else 'change()'
            if case == 'replacement':
                # Explicit identical-text replacement drops unserializable pending
                # members. Restore their equivalent literal declarations afterward.
                call += ";e.setAttribute('style',e.getAttribute('style'));s.setProperty('margin-right','13px','important');s.setProperty('margin-bottom','17px','important');s.setProperty('margin-left','19px','important')"
            markup += '<script>' + script + call + '</script>'
        page.write_text(markup)
        dest = root / case
        result = subprocess.run([str(binary), '-v', '--js_trace=1',
            '--enable_javascript=' + str(int(case not in ('reference', 'disabled'))),
            '--capture-dir', str(dest), '--capture-size', '400x250', '--capture-id', 'target',
            '--capture-settle', '500', '--capture-timeout', '6000', str(page)],
            env=env, capture_output=True, text=True, timeout=10)
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
            # Release builds intentionally disable logging; the pixel oracle works
            # in both configurations. Debug additionally verifies attribution.
            if 'Console: STYLE_CHANGED' in log:
                mutations = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] dom.mutation .*operation=style\.(\S+).*result=0', log)
                renders = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] render.request .*mode=(\S+)', log)
                assert mutations and len(renders) == 1, (case, diagnostic)
                assert all(m[:2] == renders[0][:2] for m in mutations), (case, diagnostic)
                assert renders[0][2] == ('full-restyle' if case == 'timer' else 'initial-build'), (case, diagnostic)
