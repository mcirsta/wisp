#!/usr/bin/env python3
"""CSSOM View Window scrolling moves the viewport, including zoom and events."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
markup = '''<!doctype html><meta charset="utf-8"><style>
html,body{margin:0}#area{width:1200px;height:2000px;background:#eef}
#marker{position:absolute;top:700px;left:500px;width:20px;height:20px;background:red}
button{position:fixed;top:10px;left:10px;width:100px;height:30px}
#result{position:fixed;top:50px;left:10px}
</style><div id="area"><div id="marker"></div></div><button id="run">Run</button><pre id="result"></pre>
<script>
const result=document.getElementById('result'),area=document.getElementById('area');
let active=false,events=[];
function check(ok,label){if(!ok)throw Error(label)}
function near(a,b){return Math.abs(a-b)<1.1}
function pos(x,y,label){check(near(scrollX,x)&&near(scrollY,y),label+': '+scrollX+','+scrollY)}
function fail(e){result.className='FAIL';result.textContent=e.message;console.log('SCROLL_FAIL:'+e.stack)}
document.addEventListener('scroll',e=>{
 if(active)try{check(e.target===document&&e.isTrusted&&e.bubbles&&!e.cancelable,'scroll flags');events.push('scroll')}
 catch(x){fail(x)}
});
onscrollend=e=>{
 if(active)try{check(e.target===document&&e.isTrusted&&e.bubbles&&!e.cancelable,'scrollend flags');events.push('end')}
 catch(x){fail(x)}
};
document.getElementById('run').addEventListener('click',()=>{
 try{
  active=true;events=[];
  check(document.compatMode===EXPECTED_MODE,'document mode');
  const viewport=document.compatMode==='BackCompat'?document.body:document.documentElement;
  check(viewport.clientHeight<=innerHeight&&viewport.clientHeight>0,'jQuery viewport measurement');
  check(scrollTo(120,240)===undefined,'published CSSOM return value');pos(120,240,'two arguments');
  const r=document.getElementById('marker').getBoundingClientRect();
  check(near(r.x,380)&&near(r.y,460),'actual viewport-relative geometry');
  scrollTo({top:300});pos(120,300,'omitted left');
  scrollTo({left:160});pos(160,300,'omitted top');
  scrollTo();scrollTo(null);scrollTo({left:undefined});pos(160,300,'empty options');
  scrollBy({top:50});pos(160,350,'relative options');
  scrollBy(-60,-100);pos(100,250,'relative numeric');
  scroll({left:60,top:80,behavior:'smooth'});pos(60,80,'smooth may be immediate');
  scrollTo({get left(){scrollTo(30,100);return 70}});pos(70,100,'read omitted coordinate after conversion');
  window.pageYOffset=9999;window.scrollY=9999;
  scrollTo({left:80});
  check(near(pageXOffset,80),'aliases cannot override native scroll inputs');
  delete window.scrollY;delete window.pageYOffset;
  // Keep using the original native getter, captured before replacement below.
  Object.defineProperty(window,'scrollY',scrollYDescriptor);
  Object.defineProperty(window,'pageYOffset',pageYDescriptor);
  pos(80,100,'native omitted coordinate');
  document.documentElement.style.overflow='hidden';
  scrollTo(80,150);pos(80,150,'programmatic scroll with root locked');
  document.documentElement.style.overflow='';
  area.style.height='2400px';
  scrollTo(Number.MAX_VALUE,Number.MAX_VALUE);
  check(scrollX>0&&scrollY>1800&&scrollY<2400,'finite huge values clamp after layout/zoom');
  scrollTo(-1e300,-1e300);pos(0,0,'negative clamp');
  scrollTo(30,40);scrollTo(Infinity,NaN);pos(0,0,'non-finite normalization');
  scrollTo('100','200');pos(100,200,'string conversion');
  scrollTo({left:100.25,top:200.25,behavior:'instant'});
  check(Math.abs(scrollX-100.25)<1&&Math.abs(scrollY-200.25)<1,'native pixel rounding');
  scrollTo(120,240);pos(120,240,'final position');
  check(events.length===0,'events must not reenter script');
  setTimeout(()=>{
   try{
    check(events.join()==='scroll,end','coalesced event order: '+events);
    events=[];scrollTo({});scrollBy(0,0);pos(120,240,'no-op');
    setTimeout(()=>{
     try{check(events.length===0,'no-op emits no events');if(result.className!=='FAIL')result.className='PASS'}
     catch(e){fail(e)}
    },50);
   }catch(e){fail(e)}
  },50);
 }catch(e){fail(e)}
});
const scrollYDescriptor=Object.getOwnPropertyDescriptor(window,'scrollY');
const pageYDescriptor=Object.getOwnPropertyDescriptor(window,'pageYOffset');
</script>'''

with tempfile.TemporaryDirectory(prefix='wisp-js-scroll-') as tmp:
    root = Path(tmp)
    page = root / 'page.html'
    modes = {'standards': '<!doctype html>',
             'limited': '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">',
             'quirks': ''}
    for zoom, dpr, mode in ((100, 1, 'standards'), (150, 1, 'standards'), (100, 2, 'standards'),
                            (100, 1, 'limited'), (100, 1, 'quirks')):
        page.write_text(markup.replace('<!doctype html>', modes[mode]).replace(
            'EXPECTED_MODE', json.dumps('BackCompat' if mode == 'quirks' else 'CSS1Compat')))
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR=str(dpr),
                   QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
        dest = root / f'{zoom}-{dpr}-{mode}'
        p = subprocess.run([str(binary), '-v', '--js_trace=1', '--enable_javascript=1', f'--scale={zoom}',
            '--capture-dir', str(dest), '--capture-size', '400x250', '--capture-id', 'result',
            '--capture-click-id', 'run', '--capture-settle', '250', '--capture-timeout', '10000', str(page)],
            env=env, capture_output=True, text=True, timeout=20)
        log = p.stdout + p.stderr
        diagnostic = '\n'.join(line for line in log.splitlines()
                               if 'Console:' in line or 'JavaScript ' in line or 'viewport.scroll' in line)
        assert p.returncode == 0 and 'runtime error:' not in log, (zoom, dpr, log[-14000:])
        meta = json.loads((dest / 'capture.json').read_text())
        dom = (dest / 'dom.txt').read_text()
        assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostic)
        assert 'class="PASS"' in dom, (zoom, dpr, mode, dom, diagnostic)
        assert 'SCROLL_FAIL:' not in log and 'JavaScript exception' not in log, diagnostic
print('Window scroll overloads, viewport movement, layout flush, zoom, HiDPI and deferred events pass.')
