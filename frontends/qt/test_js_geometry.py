#!/usr/bin/env python3
"""CSSOM reads update layout without completing parsing or pumping JS tasks."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
prefix = '''<!doctype html><meta charset="utf-8"><title>Layout reads</title>
<style>body{margin:0}#target{width:100px;height:20px;padding:5px;border:3px solid blue;background:green}
#after{width:70px;height:15px;background:blue}#result{height:20px;background:green}</style>
<div id="target"></div><div id="result"></div>'''
script = '''
const target=document.getElementById('target'),result=document.getElementById('result');
let timer=false,microtask=false,dcl=0,loads=0;
function check(ok,message){if(!ok)throw Error(message)}
function fail(e){result.className='FAIL';result.textContent=e.message;console.log('GEOMETRY_FAIL:'+e.stack)}
document.addEventListener('DOMContentLoaded',()=>dcl++);
window.addEventListener('load',()=>loads++);
setTimeout(()=>timer=true,0); Promise.resolve().then(()=>microtask=true);
try {
  check(document.readyState==='loading','initial readiness');
  check(target.clientWidth===110 && target.clientHeight===30,'parser-time padding box');
  check(document.readyState==='loading' && dcl===0 && loads===0,'measurement preserves parsing');
  check(!timer && !microtask,'measurement does not pump tasks/checkpoints');
  target.style.width='200px';
  check(target.clientWidth===210,'same-entry style write/read');
  console.log('READ_LOOP_BEGIN');
  for(let i=0;i<100;i++)check(target.clientWidth===210 && target.offsetWidth===216 &&
    target.getBoundingClientRect().width===216 && innerWidth===400,'unchanged repeated read');
  console.log('READ_LOOP_END');
  target.style.width='';
  check(target.clientWidth===110,'style removal/read');
}catch(e){fail(e)}
'''
late = '''<style>#target{width:250px}</style><div id="after"></div><script>
try {
  check(document.readyState==='loading','parser continued');
  check(target.clientWidth===260,'later parser stylesheet updates the provisional context');
  check(document.getElementById('after').clientWidth===70,'later parser element appears');
  target.style.height='40px';check(target.clientHeight===50,'second parser write/read');
  const inline=document.createElement('span'); inline.textContent='text';document.body.appendChild(inline);
  check(inline.clientWidth===0 && inline.clientHeight===0,'inline box has no client area');inline.parentNode.removeChild(inline);
  const hidden=document.createElement('div');hidden.style.display='none';document.body.appendChild(hidden);
  check(hidden.clientWidth===0,'display none');hidden.parentNode.removeChild(hidden);
  check(target.cloneNode(true).clientWidth===0,'detached clone has no box');
}catch(e){fail(e)}
window.addEventListener('load',()=>{
  try {
    check(dcl===1 && loads===1 && document.readyState==='complete','single lifecycle completion');
    target.style.width='300px';check(target.clientWidth===310,'post-load synchronous restyle');
    target.style.display='none';check(target.clientWidth===0,'hidden after rendering');
    target.style.display='block';check(target.clientWidth===310,'shown after synchronous hidden layout');
    check(document.documentElement.clientWidth===400 && document.documentElement.clientHeight===250,
      'root client area uses viewport');
    if(result.className!=='FAIL')result.className='PASS';
  }catch(e){fail(e)}
});
</script>'''

with tempfile.TemporaryDirectory(prefix='wisp-js-geometry-') as tmp:
    root = Path(tmp)
    reference = None
    for case in ('reference', 'dynamic', 'ua-only', 'interactive'):
        page = root / (case + '.html')
        markup = prefix + '<style>#target{width:300px;height:40px}</style><div id="after"></div>'
        if case == 'dynamic':
            markup = prefix + '<script>' + script + '</script>' + late
        elif case == 'ua-only':
            # No author stylesheet delays the parser's first measurement.
            markup = '''<!doctype html><body><div id="target" style="width:123px;height:17px"></div><script>
              const t=document.getElementById('target');
              if(t.clientWidth!==123 || t.clientHeight!==17)throw Error('UA-only initial layout');
              t.className='PASS';
              </script><div>Parser continued</div>'''
        elif case == 'interactive':
            # DOMContentLoaded may run before the scheduled box converter.
            markup = prefix + '''<script>document.addEventListener('DOMContentLoaded',()=>{
              const t=document.getElementById('target');
              if(document.readyState!=='interactive' || t.clientWidth!==110)throw Error('DCL layout');
              t.style.width='170px';
              if(t.clientWidth!==180)throw Error('DCL mutation/read');
              t.className='PASS';
            });</script>'''
        page.write_text(markup)
        dest = root / case
        result = subprocess.run([str(binary), '-v', '--js_trace=1', '--enable_javascript=1',
            '--capture-dir', str(dest), '--capture-size', '400x250', '--capture-id', 'target',
            '--capture-settle', '400', '--capture-timeout', '6000', str(page)],
            env=env, capture_output=True, text=True, timeout=12)
        log = result.stdout + result.stderr
        diagnostic = '\n'.join(line for line in log.splitlines()
                               if 'JavaScript ' in line or 'Console:' in line or '[JS ' in line)
        assert result.returncode == 0 and 'runtime error:' not in log, (case, log[-14000:])
        meta = json.loads((dest / 'capture.json').read_text())
        assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, diagnostic)
        assert 'JavaScript exception' not in log and 'GEOMETRY_FAIL:' not in log, (case, diagnostic)
        if 'Console: READ_LOOP_BEGIN' in log:
            reads = log.split('Console: READ_LOOP_BEGIN', 1)[1].split('Console: READ_LOOP_END', 1)[0]
            assert 'layout_document: PROFILER: START' not in reads, 'Unchanged reads repeat layout'
        pixels = (dest / 'page.png').read_bytes()
        if case == 'reference':
            reference = pixels
        else:
            dom = (dest / 'dom.txt').read_text()
            assert 'class="PASS"' in dom, (case, dom, diagnostic)
            if case == 'dynamic':
                assert pixels == reference, 'Forced layout changes final rendering'
print('Parser-time, mutation, stylesheet, client-size and lifecycle layout reads pass.')
