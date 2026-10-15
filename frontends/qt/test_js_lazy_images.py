#!/usr/bin/env python3
"""Unmodified site lazySizes: viewport loading, autosizes, cache and failures."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

binary = Path(sys.argv[1]).resolve()
repo = Path(__file__).resolve().parents[2]
loader = (repo / 'test/js-lazy-images/vendor/lazysizes-4.0.3.min.js').read_bytes()
bitmap = (repo / 'test/test_data/object_fit/test_wide.png').read_bytes()
placeholder = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7'
requests = []


def img(name, source=None, extra=''):
    return (f'<img id="{name}" class="lazyload" src="{placeholder}" data-src="/{source or name}.png" '
            f'data-srcset="/{source or name}.png 400w" data-sizes="auto" {extra}>')


page = '''<!doctype html><meta charset="utf-8"><style>
body{margin:16px}article{width:320px;max-width:100%;padding-top:50px}
img{display:block;width:100%;height:auto}.gap{height:2200px}
button{position:fixed;left:10px;top:10px;width:60px;height:30px;z-index:10}
#hidden{visibility:hidden;position:absolute;top:80px;left:360px;width:100px}
#clip{position:absolute;top:80px;left:480px;width:100px;height:100px;overflow:hidden}
#clipped{position:absolute;top:200px;width:100px}
</style><button id="verify">Check</button><div id="result"></div><article>''' + img('initial') + '''
<div class="gap"></div>''' + img('later') + img('cached', 'initial') + img('broken') + '''</article>''' + img('hidden') + '''
<div id="clip">''' + img('clipped', extra='data-expand="-1"') + '''</div>
<script>
window.lazySizesConfig={loadHidden:false};
const result=document.getElementById('result');
let phase=0, errors=0, loads={initial:0,later:0,cached:0};
function check(ok,label){if(!ok)throw Error(label)}
function fail(e){result.className='FAIL';console.log('LAZY_FAIL:'+e.message+' '+e.stack)}
for(const id of Object.keys(loads))document.getElementById(id).addEventListener('load',e=>{
  if(!e.target.currentSrc.startsWith('data:'))loads[id]++;
});
document.getElementById('broken').addEventListener('error',()=>errors++);
function loaded(id,width){
  const img=document.getElementById(id),r=img.getBoundingClientRect();
  check(img.complete && img.naturalWidth===width && img.naturalHeight===width*3/4,
    id+' state '+[img.complete,img.naturalWidth,img.naturalHeight]);
  check(Math.abs(r.width-width)<1 && Math.abs(r.height-width*3/4)<1,id+' layout '+JSON.stringify(r));
  check(img.sizes===width+'px' && img.classList.contains('lazyloaded'),id+' autosizes and loaded class');
}
document.getElementById('verify').addEventListener('click',()=>{
  try{
    check('onscroll' in window && lazySizes.cfg.loadHidden===false,'normal viewport path');
    const width=innerWidth<352?innerWidth-32:320;
    loaded('initial',width);
    if(phase===0){
      check(loads.initial===1,'initial single load');
      for(const id of ['later','cached','broken','hidden','clipped']){
        const img=document.getElementById(id);
        check(img.currentSrc.startsWith('data:') && img.classList.contains('lazyload'),id+' still deferred');
      }
    }else{
      loaded('later',width);loaded('cached',width);
      const broken=document.getElementById('broken');
      check(broken.complete && broken.naturalWidth===0 && errors>=1,'failed resource completion');
      if(phase===1)check(loads.initial===1 && loads.later===1 && loads.cached===1,'one load per image');
      check(document.readyState==='complete','document completion');
    }
    phase++;
    if(result.className!=='FAIL')result.className='PASS'+phase;
  }catch(e){fail(e)}
});
</script><script src="/loader.js"></script>'''


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        code, mime = 200, 'text/html; charset=utf-8'
        if self.path == '/page':
            body = page.encode()
        elif self.path == '/loader.js':
            body, mime = loader, 'text/javascript'
        elif self.path in ('/initial.png', '/later.png', '/hidden.png', '/clipped.png'):
            body, mime = bitmap, 'image/png'
            time.sleep(.15)
        else:
            body, code = b'missing', 404
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'max-age=3600')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-js-lazy-') as tmp:
        dest = Path(tmp)
        p = subprocess.run([str(binary), '-v', '--verbose_filter=level:INFO', '--enable_javascript=1',
            '--capture-dir', str(dest), '--capture-size', '900x700', '--capture-settle', '700',
            '--capture-timeout', '18000', '--capture-id', 'result',
            '--capture-click-point', '30,20', '--capture-scroll', '0,2450',
            '--capture-click-point', '30,20', '--capture-resize', '260x700',
            '--capture-click-point', '30,20',
            f'http://127.0.0.1:{server.server_port}/page'],
            env=env, capture_output=True, text=True, timeout=25)
        log = p.stdout + p.stderr
        diagnostics = '\n'.join(line for line in log.splitlines() if 'LAZY_FAIL:' in line or 'JavaScript ' in line)
        assert p.returncode == 0 and 'runtime error:' not in log, log[-14000:]
        meta = json.loads((dest / 'capture.json').read_text())
        dom = (dest / 'dom.txt').read_text()
        assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostics)
        assert 'class="PASS3"' in dom, (dom, diagnostics)
        steps = json.loads((dest / 'steps.json').read_text())
        assert all(s['active_resources'] == 0 for s in steps), steps
        assert 'LAZY_FAIL:' not in log and 'JavaScript exception' not in log, diagnostics
        assert requests.count('/initial.png') == 1 and requests.count('/later.png') == 1, requests
        assert '/hidden.png' not in requests and '/clipped.png' not in requests, requests
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print('Real lazySizes: initial/deferred images, scroll, resize autosizes, cache, failure and completion pass.')
