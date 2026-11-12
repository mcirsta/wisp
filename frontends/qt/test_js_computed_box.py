#!/usr/bin/env python3
"""CSSOM resolved box values: used geometry versus computed values (CSSOM §9)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
markup = r'''<!doctype html><meta charset="utf-8"><title>Resolved box styles</title>
<style>
html{display:inline} body{margin:0;font:16px/20px sans-serif}
#parent{width:200px;height:120px}
#content{width:92px;height:30px;padding:2px;margin:3px;border:1px solid}
#border{box-sizing:border-box;width:100px;height:40px;padding:2px;border:1px solid}
#percent{width:50%;height:25%;padding:5%;margin:0 auto}
#math{width:calc(50% - 10px);height:calc(10px + 5px)}
#hidden{display:none;width:50%;height:auto;padding:5%;margin:1em auto;border:2.5px solid}
#hidden b{width:75%;height:auto;padding:1em}
#inline{width:99px;height:88px;margin:7px 8px 9px 10px;padding:3px 4px 5px 6px}
#flex{display:flex} #grid{display:grid}
#float{float:left} #absolute{position:absolute}
#pseudo::before{content:'';display:block;width:50%;height:10px;padding:3px}
#pseudo::after{display:block;width:25%;height:12px}
#pseudo{width:100px}
#positioned{position:relative;width:200px;height:100px;padding:10px;border:2px solid}
#abs{position:absolute;left:10%;top:10px;width:40px;height:20px;padding:2px;border:1px solid;margin:3px}
#relative{position:relative;left:10%;bottom:5px;width:20px;height:10px}
#fixed{position:fixed;right:20px;bottom:30px;width:40px;height:20px}
#scroll{width:100px;height:60px;padding:5px;overflow:scroll}
#sticky{position:sticky;top:10%;width:20px;height:10px}
#cell{padding:3px;vertical-align:middle}
#inlinepseudo::before,#inlinepseudo::after{content:'';position:absolute;width:20%;height:5px}

</style><div id="parent"><div id="content"></div><div id="border"></div><div id="percent"></div>
<div id="math"></div><div id="hidden"><b id="hiddenchild"></b></div></div>
<div><span id="inline">inline</span></div><div id="flex"><span id="flexitem">flex</span></div>
<div id="grid"><span id="griditem">grid</span></div><span id="float">float</span>
<span id="absolute">absolute</span><div id="pseudo"></div>
<div id="positioned"><div id="abs"></div><div id="relative"></div><span id="inlinepseudo">x</span></div><div id="fixed"></div>
<div id="scroll"><div id="sticky"></div></div>
<table><tr><td id="cell">short</td><td style="height:80px">tall</td></tr></table><pre id="result"></pre>
<script>
const result=document.getElementById('result'), steps=[];
function check(actual,expected,label){if(actual!==expected)throw Error(label+': '+actual+' != '+expected)}
function style(id){return getComputedStyle(document.getElementById(id))}
function run(stage){try{
  check(getComputedStyle(document.documentElement).display,'block','root blockification');
  const c=style('content'), b=style('border'), p=style('percent'), h=style('hidden');
  check(c.width,'92px','content width');check(c.height,'30px','content height');
  check(c.padding,'2px','padding shorthand');// CSS2 §10.3.3 solves the over-constrained LTR block's end margin.
  check(c.margin,'3px 99px 3px 3px','used block margin shorthand');
  check(c.borderWidth,'1px','border width shorthand');check(c.borderStyle,'solid','border style shorthand');
  check(c.getPropertyValue('PADDING-LEFT'),'2px','canonical name');
  check(c.boxSizing,'content-box','content sizing');check(b.boxSizing,'border-box','border sizing');
  check(b.width,'100px','border width');check(b.height,'40px','border height');
  check(p.width,'100px','percentage width');check(p.height,'30px','percentage height');
  check(p.padding,'10px','percentage padding');check(p.marginLeft,'40px','auto left margin');
  check(p.marginRight,'40px','auto right margin');
  check(style('math').width,'90px','used calc');check(style('math').height,'15px','absolute calc');
  check(h.display,'none','hidden display');check(h.width,'50%','hidden percentage');
  check(h.height,'auto','hidden auto');check(h.padding,'5%','hidden padding');
  check(h.margin,'16px auto','hidden font-relative margin');check(h.borderWidth,'2.5px','computed fractional border');
  check(style('hiddenchild').width,'75%','hidden descendant');
  check(style('hiddenchild').padding,'16px','hidden descendant relative length');
  const i=style('inline');check(i.width,'99px','inline width does not apply');
  check(i.height,'88px','inline height does not apply');check(i.padding,'3px 4px 5px 6px','inline edges');
  check(i.margin,'7px 8px 9px 10px','inline margins');
  for(const id of ['flexitem','griditem','float','absolute'])check(style(id).display,'block',id+' blockification');
  check(style('float').cssFloat,'left','float');check(style('absolute').position,'absolute','position');
  const pseudo=document.getElementById('pseudo');
  check(getComputedStyle(pseudo,'::before').width,'50px','generated pseudo used width');
  check(getComputedStyle(pseudo,'::before').padding,'3px','generated pseudo padding');
  check(getComputedStyle(pseudo,'::after').width,'25%','ungenerated pseudo computed width');
  const inlinepseudo=document.getElementById('inlinepseudo');
  check(getComputedStyle(inlinepseudo,'::before').width,'44px','inline owner before');
  check(getComputedStyle(inlinepseudo,'::after').width,'44px','inline owner after');
  check(c.top,'auto','static auto inset');
  document.getElementById('content').style.top='25%';check(c.top,'25%','static specified inset');
  const positioned=style('abs'), rel=style('relative'), fixed=style('fixed');
  check(positioned.left,'22px','absolute percentage padding-box reference');
  check(positioned.top,'10px','absolute top');check(positioned.right,'146px','absolute auto right');
  check(positioned.bottom,'78px','absolute auto bottom');
  check(rel.left,'20px','relative percentage');check(rel.right,'-20px','relative auto opposite');
  check(rel.bottom,'5px','relative bottom');check(rel.top,'-5px','relative auto top');
  document.getElementById('relative').style.right='25%';
  check(rel.left,'20px','relative winning inset');check(rel.right,'25%','overconstrained inset stays computed');
  document.getElementById('relative').style.right='';
  document.getElementById('relative').style.cssText='left:auto;right:calc(10% + 3px)';
  check(rel.left,'-23px','relative opposite calc');check(rel.right,'23px','relative calc');
  document.getElementById('relative').style.cssText='';
  check(fixed.right,'20px','fixed right');check(fixed.bottom,'30px','fixed bottom');
  check(fixed.left,'340px','fixed auto left');check(fixed.top,'450px','fixed auto top');
  check(style('scroll').padding,'5px','scrollbar space is not CSS padding');
  check(style('scroll').width,'100px','scrollbar content sizing');
  check(style('cell').padding,'3px','table alignment space is not CSS padding');
  check(style('sticky').top.endsWith('px'),true,'sticky percentage resolves');
  check(style('sticky').bottom,'auto','sticky auto stays auto');
  const el=document.getElementById('content');
  el.style.borderStyle='none';check(c.borderWidth,'0px','none border computed width');
  el.style.borderStyle='hidden';check(c.borderWidth,'0px','hidden border computed width');
  el.style.borderStyle='solid';
  el.style.width='50%';check(c.width,'100px','live width');
  document.getElementById('parent').style.width='240px';check(c.width,'120px','live containing width');
  el.style.display='none';check(c.width,'50%','live used to computed');
  el.style.cssText='';document.getElementById('parent').style.cssText='';
  check(c.width,'92px','live restoration');
  console.log('BOX_READ_BEGIN');
  for(let n=0;n<100;n++){check(c.width,'92px','repeat width');check(c.padding,'2px','repeat padding')}
  console.log('BOX_READ_END');
  steps.push(stage);result.className=steps.join('-');
}catch(e){result.className='FAIL';result.textContent=e.stack;console.log('BOX_FAIL:'+e.message+' '+e.stack)}}
run('parser');window.addEventListener('load',()=>{if(result.className!=='FAIL')run('loaded')});
</script>'''
with tempfile.TemporaryDirectory(prefix='wisp-js-box-style-') as tmp:
    root = Path(tmp)
    page = root / 'index.html'
    page.write_text(markup)
    dest = root / 'capture'
    p = subprocess.run([str(binary), '-v', '--js_trace=1', '--enable_javascript=1',
        '--capture-dir', str(dest), '--capture-size', '400x500', '--capture-id', 'content',
        '--capture-settle', '400', '--capture-timeout', '6000', str(page)],
        env=env, capture_output=True, text=True, timeout=15)
    log = p.stdout + p.stderr
    assert p.returncode == 0 and 'runtime error:' not in log, log[-18000:]
    meta = json.loads((dest / 'capture.json').read_text())
    dom = (dest / 'dom.txt').read_text()
    diagnostic = '\n'.join(line for line in log.splitlines() if 'JavaScript ' in line or 'Console:' in line)
    assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostic)
    assert 'class="parser-loaded"' in dom, (dom, diagnostic)
    for reads in log.split('Console: BOX_READ_BEGIN')[1:]:
        assert 'layout_document: PROFILER: START' not in reads.split('Console: BOX_READ_END')[0]
print('Resolved dimensions, box edges, applicability, pseudos, live reads and layout reuse pass.')
