#!/usr/bin/env python3
"""Native scroll/resize changes reach JS once per target and scheduler turn."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
markup = '''<!doctype html><meta charset="utf-8"><style>
html,body{margin:0}body{height:2000px}#scroller{position:fixed;left:20px;top:60px;width:120px;height:70px;overflow:scroll}
#inner{height:400px;width:300px}button{position:fixed;left:10px;top:10px;width:100px;height:30px}
</style><button id="verify">Check</button><div id="scroller"><div id="inner"></div></div><pre id="result"></pre>
<script>
let ready=false,seen=[],captured=0,documentEvents=0;
const result=document.getElementById('result'),scroller=document.getElementById('scroller');
function check(ok,label){if(!ok)throw Error(label)}
function fail(e){result.className='FAIL';result.textContent=e.message;console.log('VIEW_FAIL:'+e.message+' '+e.stack)}
function flags(e,target,bubbles){check(e.target===target&&e.bubbles===bubbles&&e.isTrusted&&!e.cancelable&&!e.composed,'event flags')}
window.addEventListener('load',()=>{ready=true;seen=[];captured=documentEvents=0});
window.addEventListener('scroll',e=>{if(ready){captured++;check(e.currentTarget===window,'capture target')}},true);
document.addEventListener('scroll',e=>{if(ready){flags(e,document,true);documentEvents++}});
onscroll=e=>{if(ready)try{flags(e,document,true);seen.push('document:'+scrollY)}catch(x){fail(x)}};
scroller.addEventListener('scroll',e=>{if(ready)try{flags(e,scroller,false);seen.push('element')}catch(x){fail(x)}});
onresize=e=>{if(ready)try{flags(e,window,false);seen.push('resize:'+innerWidth+'x'+innerHeight)}catch(x){fail(x)}};
document.getElementById('verify').addEventListener('click',()=>{
  try{
    check(seen.join()==='document:200,element,resize:440x280','coalesced order: '+seen.join());
    check(captured===2&&documentEvents===1,'capture/bubble counts '+captured+'/'+documentEvents);
    if(result.className!=='FAIL')result.className='PASS';
  }catch(e){fail(e)}
});
</script>'''
with tempfile.TemporaryDirectory(prefix='wisp-js-view-') as tmp:
    root=Path(tmp); source=root/'page.html';source.write_text(markup);dest=root/'capture'
    p=subprocess.run([str(binary),'-v','--js_trace=1','--enable_javascript=1',
        '--capture-dir',str(dest),'--capture-size','400x250','--capture-settle','150','--capture-timeout','8000',
        '--capture-batch','2','--capture-scroll','0,100','--capture-scroll','0,200',
        '--capture-batch','2','--capture-scroll-element','scroller,0,20','--capture-scroll-element','scroller,0,60',
        '--capture-resize','440x280','--capture-click-point','40,20',str(source)],
        env=env,capture_output=True,text=True,timeout=15)
    log=p.stdout+p.stderr
    diagnostic='\n'.join(line for line in log.splitlines() if 'Console:' in line or 'JavaScript ' in line)
    assert p.returncode==0 and 'runtime error:' not in log,log[-14000:]
    meta=json.loads((dest/'capture.json').read_text());dom=(dest/'dom.txt').read_text()
    assert meta['result']=='complete' and meta['load_complete'],(meta,diagnostic)
    assert 'class="PASS"' in dom,(dom,diagnostic)
    assert 'VIEW_FAIL:' not in log and 'JavaScript exception' not in log,diagnostic
print('Native document/element scroll coalescing, bubbling, Window handlers and resize pass.')
