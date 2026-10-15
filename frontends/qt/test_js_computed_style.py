#!/usr/bin/env python3
"""Real selected styles, synchronous reads and live read-only CSSOM wrappers."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
markup = r'''<!doctype html><meta charset="utf-8"><title>Computed styles</title>
<style>
body{margin:0;visibility:hidden}
#target{width:100px;height:20px;--tone:.25;opacity:var(--tone);visibility:visible;overflow-x:hidden}
#target.changed{visibility:hidden}
#target::before{content:'before';opacity:.5}
#target::after{visibility:collapse}
#hidden{display:none;visibility:collapse;opacity:.25}
#hidden b{opacity:inherit}
#result{visibility:visible}
</style><div id="target"></div><div id="hidden"><i><b id="nested"></b></i></div><pre id="result"></pre>
<script>
function check(ok,message){if(!ok)throw Error(message)}
const t=document.getElementById('target'), result=document.getElementById('result');
const s=getComputedStyle(t), h=getComputedStyle(document.getElementById('nested'));
let steps=[];
function run(stage){
  try{
    check(s.opacity==='0.25' && s.visibility==='visible','selected '+stage);
    check(s.overflow==='hidden auto' && s.overflowY==='auto','mixed axes '+stage);
    check(s.cssText==='' && s.parentRule===null,'computed declaration flags');
    check(s.getPropertyPriority('opacity')==='' && s.getPropertyValue('OPACITY')==='0.25','CSSOM reads');
    check(h.visibility==='collapse' && h.opacity==='1','hidden descendant inheritance');
    document.getElementById('hidden').style.visibility='hidden';
    check(h.visibility==='hidden','hidden ancestor mutation');
    document.getElementById('hidden').style.visibility='collapse';
    t.style.opacity='.5';check(s.opacity==='0.5','live same-entry style write');t.style.opacity='';
    check(s.opacity==='0.25','cascade after removal');
    t.className='changed';check(s.visibility==='hidden','live class changes');t.className='';
    const before=getComputedStyle(t,':before'), after=getComputedStyle(t,'::after');
    check(before.opacity==='0.5' && before.visibility==='visible','pseudo composition');
    check(after.visibility==='collapse' && after.opacity==='1','non-inherited pseudo value');
    check(getComputedStyle(t,'::b\\65 fore').opacity==='0.5','CSS escaped pseudo');
    check(getComputedStyle(t,'::BEFORE').opacity==='0.5','CSS pseudo case');
    const noRule=document.createElement('div');noRule.style.visibility='hidden';document.body.appendChild(noRule);
    check(getComputedStyle(noRule,'::before').visibility==='hidden','unmatched pseudo inheritance');
    noRule.parentNode.removeChild(noRule);
    const axes=document.createElement('div');document.body.appendChild(axes);
    for(const x of ['visible','hidden','scroll','auto'])for(const y of ['visible','hidden','scroll','auto']){
      axes.style.cssText='overflow-x:'+x+';overflow-y:'+y;
      const resolved=getComputedStyle(axes), cx=x==='visible'&&y!=='visible'?'auto':x,
        cy=y==='visible'&&x!=='visible'?'auto':y;
      check(resolved.overflowX===cx && resolved.overflowY===cy &&
        resolved.overflow===(cx===cy?cx:cx+' '+cy),'overflow pair '+x+'/'+y);
    }
    axes.style.cssText='overflow:inherit';
    check(getComputedStyle(axes).overflow==='visible','overflow inheritance');
    axes.style.opacity='2';check(getComputedStyle(axes).opacity==='1','opacity upper clamp');
    axes.style.opacity='-1';check(getComputedStyle(axes).opacity==='0','opacity lower clamp');
    axes.parentNode.removeChild(axes);
    for(const p of ['::bogus','::before()','::part(x)',':marker',':before junk','::before\0']){
      const invalid=getComputedStyle(t,p);check(invalid.length===0 && invalid.opacity==='','invalid pseudo '+p);
    }
    check(getComputedStyle(t,'ignored').opacity==='0.25','non-colon pseudo ignored');
    const names=[...s];check(names.length>100 && names.join()===names.slice().sort().join(),'canonical enumeration');
    check(names.includes('overflow-x') && !names.includes('overflow') && !names.includes('grid-gap'),'longhand identities');
    check(s.item(s.length)==='' && s[s.length]===undefined,'index bounds');
    check(getComputedStyle(t)!==s,'fresh wrapper');
    const parent=t.parentNode,next=t.nextSibling;
    parent.removeChild(t);check(s.length===0 && s.opacity==='','detached live declaration');
    parent.insertBefore(t,next);check(s.opacity==='0.25','reconnected live declaration');
    console.log('COMPUTED_READ_BEGIN');
    for(let i=0;i<100;i++)check(s.opacity==='0.25' && s.visibility==='visible','repeat');
    console.log('COMPUTED_READ_END');
    steps.push(stage);result.className=steps.join('-');
  }catch(e){result.className='FAIL';result.textContent=e.stack;console.log('COMPUTED_FAIL:'+e.stack)}
}
run('parser');
window.addEventListener('load',()=>{if(result.className!=='FAIL')run('loaded')});
</script>'''
with tempfile.TemporaryDirectory(prefix='wisp-js-computed-') as tmp:
    root = Path(tmp)
    page = root / 'index.html'
    page.write_text(markup)
    dest = root / 'capture'
    p = subprocess.run([str(binary), '-v', '--js_trace=1', '--enable_javascript=1',
        '--capture-dir', str(dest), '--capture-size', '400x250', '--capture-id', 'target',
        '--capture-settle', '400', '--capture-timeout', '6000', str(page)],
        env=env, capture_output=True, text=True, timeout=12)
    log = p.stdout + p.stderr
    assert p.returncode == 0 and 'runtime error:' not in log, log[-18000:]
    meta = json.loads((dest / 'capture.json').read_text())
    dom = (dest / 'dom.txt').read_text()
    diagnostic = '\n'.join(line for line in log.splitlines() if 'JavaScript ' in line or 'Console:' in line)
    assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostic)
    assert 'class="parser-loaded"' in dom, (dom, diagnostic)
    for reads in log.split('Console: COMPUTED_READ_BEGIN')[1:]:
        assert 'layout_document: PROFILER: START' not in reads.split('Console: COMPUTED_READ_END')[0]
print('Live selected styles, hidden ancestors, pseudos, overflow and repeated-read reuse pass.')
