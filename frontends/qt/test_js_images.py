#!/usr/bin/env python3
"""Element-owned image requests: state, tasks, replacement, rendering and teardown."""
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
wide = (repo / 'test/test_data/object_fit/test_wide.png').read_bytes()
small = (repo / 'test/test_data/object_fit/test_small.png').read_bytes()
requests = []
prefix = '''<!doctype html><meta charset="utf-8"><title>Image requests</title>
<style>body{margin:0}img{display:block}#result{height:20px;background:green}</style>'''

script = r'''
const output=document.getElementById('result'), img=document.getElementById('image');
function check(ok, message){if(!ok)throw Error(message)}
function fail(e){output.className='FAIL';output.textContent=e.message;console.log('IMAGE_FAIL:'+e.stack)}
let loads=0, errors=0, windowLoads=0;
window.addEventListener('load',()=>windowLoads++);
img.addEventListener('load',e=>{
  check(e instanceof Event && !(e instanceof UIEvent) && e.isTrusted && !e.bubbles && !e.cancelable &&
    e.target===img && e.currentTarget===img,'trusted element load');
  check(img.clientWidth===img.naturalWidth,'load listener sees current image layout');
  loads++;
});
img.addEventListener('error',()=>errors++);
function once(image,type){return new Promise(resolve=>image.addEventListener(type,resolve,{once:true}))}
async function run(){
  check(img instanceof HTMLImageElement && !img.complete && img.naturalWidth===0,'initial pending state');
  await once(img,'load');
  check(img.complete && img.naturalWidth===400 && img.naturalHeight===300,'initial decoded size');
  check(img.currentSrc===img.src && img.src.endsWith('/initial.png'),'resolved source');
  const old=img.currentSrc;
  let loaded=once(img,'load');
  img.src='/replacement.png';
  check(img.complete && img.currentSrc===old && img.naturalWidth===400,'current retained before checkpoint');
  check(img.clientWidth===400 && img.complete && img.currentSrc===old,
    'forced layout must not advance the queued image update');
  await Promise.resolve();
  check(!img.complete && img.currentSrc===old && img.naturalWidth===400,'current retained while pending');
  // This restyle replaces layout boxes while the image request remains active.
  document.body.style.paddingLeft='7px';
  await loaded;
  check(loads===2 && img.complete && img.naturalWidth===100 && img.naturalHeight===75,'replacement completed');
  loaded=once(img,'load');
  img.src='/must-not-fetch.png';
  img.setAttribute('src','/initial.png');
  await loaded;
  check(loads===3 && img.naturalWidth===400 && img.complete,'coalesced cached replacement');
  loaded=once(img,'load');
  img.src=img.src;
  await loaded;
  check(loads===4 && img.complete,'same source queues load');
  const failed=once(img,'error');
  img.src='/broken.png';
  await failed;
  check(errors===1 && img.complete && img.naturalWidth===0 && img.naturalHeight===0,'broken complete state');
  loaded=once(img,'load');
  img.src='/replacement.png';
  await loaded;
  check(loads===5 && img.naturalWidth===100,'recover after error');
  const detached=document.createElement('img');
  check(detached.complete && detached.currentSrc==='' && detached.naturalWidth===0,'no-source default');
  let detachedLoads=0;
  detached.addEventListener('load',()=>detachedLoads++);
  loaded=once(detached,'load');
  detached.src='/detached.png';
  await loaded;
  check(detachedLoads===1 && detached.complete && detached.naturalWidth===400 && !detached.isConnected,'detached load');
  detached.id='detached'; document.body.appendChild(detached);
  const empty=document.createElement('img');
  const emptyError=once(empty,'error');
  empty.src='';
  check(empty.complete,'empty src complete');
  await emptyError;
  setTimeout(()=>{
    try {
      check(detachedLoads===1 && loads===5 && errors===1,'insertion and restyle do not repeat events');
      check(windowLoads===1 && document.readyState==='complete','window completion after image events');
      output.className='PASS';
    } catch(e){fail(e)}
  },180);
}
run().catch(fail);
'''


responsive_markup = """<div id="result"></div>
<img id="two" srcset="/initial.png 2x">
<img id="four" srcset="/initial.png 4x">
<img id="image" src="/must-not-fetch.png" sizes="(max-width: 600px) 100px, 320px"
 srcset="/replacement.png 100w, /initial.png 400w">
<img id="zero" sizes="0px" srcset="/initial.png 400w">
<img id="fit" srcset="/initial.png 2x" style="width:240px;height:180px;object-fit:none;background:yellow">
<img sizes="0px" srcset="/initial.png 400w" style="width:20px;height:20px;object-fit:none;background:yellow">
<img srcset="/initial.png 3x" style="width:200px;height:100px;object-fit:cover">
<script>
window.addEventListener('load',()=>{
  const two=document.getElementById('two'), four=document.getElementById('four'),
        img=document.getElementById('image'), zero=document.getElementById('zero'), result=document.getElementById('result');
  function check(ok,message){if(!ok)throw Error(message)}
  try {
    check(two.complete && four.complete && two.naturalWidth===200 && two.naturalHeight===150 &&
      four.naturalWidth===100 && four.naturalHeight===75,'shared cache, different density');
    check(img.complete && img.naturalWidth===EXPECTED_WIDTH && img.naturalHeight===EXPECTED_HEIGHT &&
      img.currentSrc.endsWith('EXPECTED_SOURCE'),'viewport sizes and candidate');
    check(zero.complete && zero.naturalWidth===0 && zero.naturalHeight===0,'zero source size');
    four.addEventListener('load',()=>{
      try {check(four.complete && four.naturalWidth===200 && four.naturalHeight===150,'same URL density changed');
        result.className='PASS'}
      catch(e){result.className='FAIL';result.textContent=e.message}
    },{once:true});
    four.srcset='/initial.png 2x';
  }catch(e){result.className='FAIL';result.textContent=e.message}
});
</script>"""

class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        code, mime, delay = 200, 'text/html; charset=utf-8', 0
        if self.path == '/state':
            body = (prefix + '<div id="result"></div><img id="image" src="/initial.png"><script>' + script + '</script>').encode()
        elif self.path == '/reference':
            body = (prefix + '<body style="padding-left:7px"><div id="result" class="PASS"></div>'
                    '<img id="image" src="/replacement.png"><img id="detached" src="/detached.png">').encode()
        elif self.path in ('/responsive', '/responsive-narrow'):
            narrow = self.path.endswith('-narrow')
            body = (prefix + responsive_markup.replace('EXPECTED_WIDTH', '100' if narrow else '320')
                    .replace('EXPECTED_HEIGHT', '75' if narrow else '240')
                    .replace('EXPECTED_SOURCE', '/replacement.png' if narrow else '/initial.png')).encode()
        elif self.path in ('/responsive-reference', '/responsive-narrow-reference'):
            narrow = 'narrow' in self.path
            width, height, source = (100, 75, '/replacement.png') if narrow else (320, 240, '/initial.png')
            body = (prefix + '<div id="result" class="PASS"></div>'
                    '<img id="two" src="/initial.png" width="200" height="150">'
                    '<img id="four" src="/initial.png" width="200" height="150">'
                    f'<img id="image" src="{source}" width="{width}" height="{height}">'
                    '<img id="zero" src="/initial.png" width="0" height="0">'
                    '<div style="width:240px;height:180px;background:yellow;display:flex;align-items:center;justify-content:center">'
                    '<img src="/initial.png" width="200" height="150"></div>'
                    '<div style="width:20px;height:20px;background:yellow"></div>'
                    '<img src="/initial.png" style="width:200px;height:100px;object-fit:cover">').encode()
        elif self.path == '/supersede':
            body = (prefix + '''<div id="result"></div><img id="image"><script>
const img=document.getElementById('image'), result=document.getElementById('result');
let loads=0, errors=0;
img.addEventListener('load',()=>loads++);img.addEventListener('error',()=>errors++);
img.src='/canceled.png';
setTimeout(()=>img.src='/initial.png',30);
setTimeout(()=>{result.className=loads===1 && errors===0 && img.complete &&
  img.currentSrc.endsWith('/initial.png') && img.naturalWidth===400 ? 'PASS' : 'FAIL'},1000);
</script>''').encode()
        elif self.path == '/disabled':
            body = (prefix + '''<div id="result"></div><img id="image" src="/disabled.png"><script>
const img=document.getElementById('image');let events=0;
img.addEventListener('load',()=>events++);img.addEventListener('error',()=>events++);
setTimeout(()=>{document.getElementById('result').className=!img.complete &&
  img.naturalWidth===0 && img.currentSrc==='' && events===0 ? 'PASS' : 'FAIL'},200);
</script>''').encode()
        elif self.path == '/navigate':
            body = (prefix + '<img src="/very-slow.png"><script>setTimeout(()=>location.assign("/next"),100)</script>').encode()
        elif self.path == '/next':
            body = (prefix + '<div id="result" class="PASS">Navigated</div>').encode()
        elif self.path in ('/initial.png', '/replacement.png', '/detached.png', '/very-slow.png', '/canceled.png'):
            body, mime = (small if self.path == '/replacement.png' else wide), 'image/png'
            delay = .8 if self.path in ('/very-slow.png', '/canceled.png') else .15
        else:
            body, code = b'missing', 404
        time.sleep(delay)
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
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-js-images-') as tmp:
        state_pixels = None
        for case in ('state', 'reference', 'responsive', 'responsive-reference', 'responsive-narrow',
                     'responsive-narrow-reference', 'supersede', 'disabled', 'navigate'):
            requests.clear()
            dest = Path(tmp) / case
            result = subprocess.run([str(binary), '-v', '--js_trace=1', '--enable_javascript=1',
                '--foreground_images=' + str(int(case != 'disabled')),
                '--capture-dir', str(dest), '--capture-size', '360x900' if 'narrow' in case else '800x900',
                '--capture-id', 'result', '--capture-id', 'image', '--capture-id', 'detached',
                '--capture-id', 'two', '--capture-id', 'four', '--capture-id', 'zero',
                '--capture-settle', '1200' if case == 'supersede' else '600',
                '--capture-timeout', '6000', origin + '/' + case],
                env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            diagnostic = '\n'.join(line for line in log.splitlines()
                                   if 'JavaScript ' in line or 'Console: ' in line or '[JS ' in line)
            assert result.returncode == 0 and 'runtime error:' not in log, (case, log[-10000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, diagnostic)
            dom = (dest / 'dom.txt').read_text()
            assert 'class="PASS"' in dom, (case, dom, diagnostic)
            assert 'JavaScript exception' not in log and 'IMAGE_FAIL:' not in log, (case, diagnostic)
            if case == 'state':
                state_pixels = (dest / 'page.png').read_bytes()
                inspection = json.loads((dest / 'inspection.json').read_text())
                assert inspection['active_resources'] == 0, inspection
                elements = {e['id']: e for e in inspection['elements']}
                for name, width, height in (('image', 100, 75), ('detached', 400, 300)):
                    box = elements[name]['boxes'][0]['content']
                    assert (box['width'], box['height']) == (width, height), (name, box)
                assert '/must-not-fetch.png' not in requests, requests
                for url in ('/initial.png', '/replacement.png', '/detached.png', '/broken.png'):
                    assert requests.count(url) == 1, requests
            elif case in ('responsive', 'responsive-narrow'):
                responsive_pixels = (dest / 'page.png').read_bytes()
                inspection = json.loads((dest / 'inspection.json').read_text())
                assert inspection['active_resources'] == 0, inspection
                elements = {e['id']: e for e in inspection['elements']}
                expected = [('two', 200, 150), ('four', 200, 150), ('zero', 0, 0),
                            ('image', 100, 75) if 'narrow' in case else ('image', 320, 240)]
                for name, width, height in expected:
                    box = elements[name]['boxes'][0]['content']
                    assert (box['width'], box['height']) == (width, height), (case, name, box)
                assert '/must-not-fetch.png' not in requests, requests
                assert requests.count('/initial.png') == 1, requests
                assert requests.count('/replacement.png') == int('narrow' in case), requests
            elif case in ('responsive-reference', 'responsive-narrow-reference'):
                assert (dest / 'page.png').read_bytes() == responsive_pixels, (case, 'Responsive pixels differ from reference')
            elif case == 'navigate':
                assert meta['content_url'] == origin + '/next', meta
                assert '/very-slow.png' in requests, requests
            elif case == 'disabled':
                assert '/disabled.png' not in requests, requests
            elif case == 'supersede':
                assert '/canceled.png' in requests and '/initial.png' in requests, requests
            elif case == 'reference':
                assert (dest / 'page.png').read_bytes() == state_pixels, 'Dynamic images differ from static pixels'
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('Image request state, cache, replacement, failure, detached loading and navigation pass.')
