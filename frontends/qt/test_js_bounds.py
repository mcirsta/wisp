#!/usr/bin/env python3
"""CSSOM View rectangles/offsets: static, transformed and native scrolling."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
page = '''<!doctype html><meta charset="utf-8"><title>Box measurements</title>
<style>
html,body{margin:0}body{height:1800px}
#target{position:absolute;left:30px;top:40px;width:100px;height:50px;padding:10px;border:2px solid blue}
#parent{position:absolute;left:200px;top:100px;width:100px;height:100px;transform:translate(20px,10px)}
#nested{position:absolute;left:10px;top:5px;width:40px;height:20px;transform:scale(2)}
#scroller{position:absolute;left:200px;top:200px;width:100px;height:70px;padding:5px;border:2px solid;overflow:scroll}
#inner{width:150px;height:300px}
#fixed{position:fixed;left:10px;top:10px;width:20px;height:20px}
#stickyParent{position:absolute;left:0;top:150px;width:100px;height:900px}
#sticky{position:sticky;top:0;height:30px}
#measure{position:fixed;left:350px;top:5px;width:40px;height:25px}
#wrapped{position:absolute;left:10px;top:900px;width:70px;font:20px/20px monospace}
#split{position:absolute;left:10px;top:1000px;width:140px;font:20px/20px monospace}
#splitblock{width:110px;height:30px}
</style>
<div id="target"></div><div id="parent"><div id="nested"></div></div>
<div id="scroller"><div id="inner"></div></div><div id="fixed"></div>
<div id="stickyParent"><div id="sticky"></div></div>
<div id="wrapped"><span id="inline">AA BB CC DD</span></div>
<div id="split"><span id="splitinline">A<div id="splitblock"></div>B</span></div>
<button id="measure">Read</button><div id="result"></div>
<script>
function check(ok,message){if(!ok)throw Error(message)}
const result=document.getElementById('result');
function fail(e){result.className='FAIL';result.textContent=e.message;console.log('BOUNDS_FAIL:'+e.message+' '+e.stack)}
function rect(id,x,y,w,h){
  const r=document.getElementById(id).getBoundingClientRect();
  check(r instanceof DOMRect && r instanceof DOMRectReadOnly,'rectangle interface');
  check(Math.abs(r.x-x)<0.00001 && Math.abs(r.y-y)<0.00001 && Math.abs(r.width-w)<0.00001 && Math.abs(r.height-h)<0.00001,
    id+': '+JSON.stringify(r)+' expected '+[x,y,w,h]);return r;
}
window.addEventListener('load',()=>{
  try {
    check(innerWidth===400 && innerHeight===250 && document.documentElement.clientWidth===400 &&
      document.documentElement.clientHeight===250,'viewport dimensions');
    check(scrollX===0 && scrollY===0 && pageXOffset===0 && pageYOffset===0,'initial viewport scroll');
    const viewport=Object.getOwnPropertyDescriptor(window,'innerWidth');
    check(viewport.enumerable && viewport.configurable && typeof viewport.get==='function','viewport descriptor');
    let bad=false;try{viewport.get.call({})}catch(e){bad=e instanceof TypeError}check(bad,'viewport receiver');
    innerWidth='replacement';check(innerWidth==='replacement','replaceable viewport value');
    Object.defineProperty(window,'innerWidth',viewport);
    const t=document.getElementById('target');
    const saved=rect('target',30,40,124,74);
    check(t.offsetWidth===124 && t.offsetHeight===74 && t.clientWidth===120,'border and padding dimensions');
    check(t.offsetParent===document.body && t.offsetLeft===30 && t.offsetTop===40,'absolute offset coordinates');
    t.style.transform='translate(100%,100%)';rect('target',154,114,124,74);
    check(saved.x===30 && saved.y===40,'rectangles are snapshots');
    t.style.transform='scale(2)';rect('target',-32,3,248,148);
    check(t.offsetWidth===124 && t.offsetHeight===74,'offset dimensions ignore transforms');
    t.style.transform='rotate(90deg)';rect('target',55,15,74,124);
    t.style.transform='translate(20px,10px) scale(2)';rect('target',-12,13,248,148);
    t.style.fontSize='10px';t.style.transform='translate(2em,1em)';rect('target',50,50,124,74);
    t.style.transform='none';
    rect('nested',210,105,80,40);
    const nested=document.getElementById('nested');
    check(nested.offsetParent===document.getElementById('parent') && nested.offsetLeft===10 && nested.offsetTop===5,'nested offset parent');
    rect('fixed',10,10,20,20);check(document.getElementById('fixed').offsetParent===null,'viewport fixed offset parent');
    rect('sticky',0,150,100,30);
    const inline=document.getElementById('inline'), r=inline.getBoundingClientRect();
    check(r.width>20 && r.width<=70 && r.height>20 && inline.offsetWidth===Math.round(r.width) &&
      inline.offsetHeight===Math.round(r.height),'wrapped inline fragments');
    check(document.getElementById('splitinline').offsetWidth===110,'inline split by block includes block width');
    t.style.display='none';rect('target',0,0,0,0);check(t.offsetParent===null && t.offsetWidth===0,'hidden offsets');
    t.style.display='block';
    const detached=t.cloneNode();const d=detached.getBoundingClientRect();
    check(d.x===0 && d.y===0 && d.width===0 && d.height===0 && detached.offsetParent===null,'detached bounds');
    if(result.className!=='FAIL')result.className='INITIAL_PASS';
  }catch(e){fail(e)}
});
document.getElementById('measure').addEventListener('click',()=>{
  try {
    check(innerWidth===400 && innerHeight===250 && scrollX===0 && scrollY===400 &&
      pageXOffset===scrollX && pageYOffset===scrollY,'scrolled viewport dimensions and aliases');
    rect('target',30,-360,124,74);rect('fixed',10,10,20,20);rect('sticky',0,0,100,30);
    rect('inner',207,-243,150,300);
    const t=document.getElementById('target');check(t.offsetTop===40 && t.offsetLeft===30,'offsets ignore scrolling');
    if(result.className!=='FAIL')result.className='PASS';
  }catch(e){fail(e)}
});
</script>'''

with tempfile.TemporaryDirectory(prefix='wisp-js-bounds-') as tmp:
    root = Path(tmp)
    source = root / 'page.html'
    source.write_text(page)
    dest = root / 'capture'
    process = subprocess.run([str(binary), '-v', '--enable_javascript=1', '--font_min_size=0', '--capture-dir', str(dest),
        '--capture-size', '400x250', '--capture-settle', '150', '--capture-timeout', '12000',
        '--capture-scroll-element', 'scroller,0,50', '--capture-scroll', '0,400',
        '--capture-click-point', '370,15', str(source)], env=env, capture_output=True, text=True, timeout=18)
    log = process.stdout + process.stderr
    diagnostic = '\n'.join(line for line in log.splitlines() if 'BOUNDS_FAIL:' in line or 'JavaScript ' in line)
    assert process.returncode == 0 and 'runtime error:' not in log, log[-14000:]
    meta = json.loads((dest / 'capture.json').read_text())
    assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostic)
    dom = (dest / 'dom.txt').read_text()
    assert 'class="PASS"' in dom, (dom, diagnostic)
    assert 'BOUNDS_FAIL:' not in log and 'JavaScript exception' not in log, diagnostic
print('Bounding rectangles, offsets, transforms, fragments, fixed/sticky and ancestor scrolling pass.')
