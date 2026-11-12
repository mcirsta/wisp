#!/usr/bin/env python3
"""Real HTTP requests through Qt's scheduler: XHR state, security and lifetime."""
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
requests = []
headers_seen = threading.Event()
script = r'''
const out=document.getElementById('result');
function check(ok,msg){if(!ok)throw Error(msg)}
function pause(n){return new Promise(r=>setTimeout(r,n))}
function runRequest(path,setup){return new Promise((resolve,reject)=>{
 const x=new XMLHttpRequest();let events=[];
 for(const name of ['readystatechange','loadstart','progress','load','error','abort','timeout','loadend'])
  x.addEventListener(name,e=>{
   try{
    check(e.target===x && e.currentTarget===x && e.isTrusted && !e.bubbles && !e.cancelable,'trusted target '+name);
    check(name==='readystatechange' ? e instanceof Event && !(e instanceof ProgressEvent) : e instanceof ProgressEvent,'event interface');
    events.push(name+':'+x.readyState);
   }catch(e){reject(e)}
  });
 x.open('GET',path);x.onloadend=()=>resolve({x,events});if(setup)setup(x);x.send();
})}
async function run(){
 let windowLoads=0;window.addEventListener('load',()=>windowLoads++);
 let returned=false,seen2=false,seen3=false,first=true,parts=[],old;
 let result=await runRequest('/stream',x=>{
   old=x;
   x.setRequestHeader('X-Probe',' first ');x.setRequestHeader('x-probe','second');
   x.setRequestHeader('Cookie','forged');x.setRequestHeader('Host','evil.invalid');
   x.setRequestHeader('X-Empty','');
   x.onreadystatechange=()=>{
     if(x.readyState===2){seen2=true;check(returned && x.status===200 && x.responseText==='','async headers');}
     if(x.readyState===3){seen3=true;parts.push(x.responseText)}
   };
   x.onprogress=e=>{check(e.lengthComputable && e.total===14 && e.loaded<=14,'byte counts');};
   check(x.status===0 && x.statusText==='' && x.responseURL==='' && x.getAllResponseHeaders()==='','pre-response state');
   setTimeout(()=>returned=true,0);
 });
 let x=result.x;
 check(seen2 && seen3 && x.readyState===4 && x.status===200 && x.statusText==='Read Me','status');
 x.responseType='unknown';
 check(x.responseText==='Salut România' && x.response===x.responseText,'UTF-8 body: '+x.responseText);
 check(parts.length>1 && parts[0]==='Salut ','streaming response');
 check(x.getResponseHeader('X-MiXeD')==='one, two' && x.getResponseHeader('set-cookie')===null,'filtered combined headers');
 check(x.getAllResponseHeaders().includes('x-mixed: one, two\r\n') && !x.getAllResponseHeaders().includes('set-cookie'),'all headers');
 check(result.events[0]==='readystatechange:1' && result.events[1]==='loadstart:1' && result.events.slice(-3).join()==='readystatechange:4,load:4,loadend:4','event order '+result.events);
 check(windowLoads===1 && document.readyState==='complete','XHR does not block Window load');
 result=await runRequest('/headers-first',x=>{x.onreadystatechange=()=>{
   if(x.readyState===2){const ack=new XMLHttpRequest();ack.open('GET','/ack-headers');ack.send();}
 }});
 check(result.x.responseText==='headers observed','HEADERS_RECEIVED before body');
 result=await runRequest('/redirect#ignore');x=result.x;
 check(x.responseText==='body' && x.responseURL===location.origin+'/ok' && x.status===200,'redirect final response '+x.responseURL);
 for(const code of [304,401,404,500]){
  result=await runRequest('/status/'+code);x=result.x;
  check(x.status===code && result.events.includes('load:4') && !result.events.includes('error:4'),'HTTP status is not network error '+code);
  check(x.responseText===(code===304?'':'status '+code),'raw HTTP body '+code);
 }
 for(const path of ['/cross','http://localhost:PORT/forbidden','/loop']){
  result=await runRequest(path);x=result.x;
  check(x.status===0 && x.responseURL==='' && x.responseText==='' && x.getAllResponseHeaders()==='' && result.events.includes('error:4'),'blocked request '+path);
 }
 result=await runRequest('/slow',x=>x.timeout=40);
 check(result.x.status===0 && result.events.includes('timeout:4') && !result.events.includes('load:4'),'timeout');
 let aborted=new XMLHttpRequest(),abortEvents=[];
 aborted.open('GET','/abort');
 for(const name of ['readystatechange','abort','loadend'])aborted.addEventListener(name,()=>abortEvents.push(name+':'+aborted.readyState));
 aborted.send();await pause(40);aborted.abort();
 check(aborted.readyState===0 && abortEvents.slice(-3).join()==='readystatechange:4,abort:4,loadend:4','abort order '+abortEvents);
 let superseded=new XMLHttpRequest(),stale=0;
 superseded.open('GET','/replaced');superseded.onload=()=>stale++;superseded.send();await pause(30);
 await new Promise((resolve,reject)=>{
   superseded.open('GET','/ok');superseded.onload=resolve;superseded.onerror=()=>reject(Error('reuse failed'));superseded.send();
 });
 check(stale===0 && superseded.responseText==='body','open cancels previous response');
 // Reopening in HEADERS_RECEIVED must suppress the old LOADING/DONE events.
 let reentrant=new XMLHttpRequest(),once=true,loads=0;
 await new Promise((resolve,reject)=>{
  reentrant.open('GET','/stream');reentrant.onreadystatechange=()=>{
   if(reentrant.readyState===2 && once){once=false;reentrant.open('GET','/ok');reentrant.send();}
  };reentrant.onload=()=>{loads++;resolve()};reentrant.onerror=()=>reject(Error('reentrant'));reentrant.send();
 });
 check(loads===1 && reentrant.responseText==='body','reentrant replacement');
 // Cancel synchronously in loadstart: no request must reach the server.
 let no=new XMLHttpRequest();no.open('GET','/must-not-fetch');no.onloadstart=()=>no.abort();no.send();check(no.readyState===0,'loadstart abort');
 for(const [path,text] of [['/latin','café €'],['/bom','雪'],['/invalid','a\ufffdb'],['/bad-prefix','a\ufffdb'],['/bad-utf16','\ufffdA']]){
  result=await runRequest(path);check(result.x.responseText===text,'decode '+path+': '+result.x.responseText);
 }
 result=await runRequest('/latin',x=>x.overrideMimeType('text/plain; charset=windows-1252'));
 check(result.x.responseText==='café €','override MIME');
 result=await runRequest('/latin',x=>x.overrideMimeType('text/plain'));
 check(result.x.responseText==='café €','MIME override preserves unspecified charset');
 await pause(400);check(stale===0 && loads===1,'late callback suppressed');
 out.className='PASS';out.textContent='XHR passed';
}
run().catch(e=>{out.className='FAIL: '+e.message;console.log('XHR_FAIL:'+e.stack)});
'''


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        requests.append((self.path, dict(self.headers)))
        path = self.path
        if path == '/test':
            body = ('<!doctype html><meta charset="utf-8"><div id="result"></div><script>' +
                    script.replace('PORT', str(self.server.server_port)) + '</script>').encode()
            self.respond(200, body, 'text/html')
        elif path == '/navigate':
            self.respond(200, b'''<div id="result"></div><script>
            let x=new XMLHttpRequest();x.open('GET','/retired');x.onload=()=>{throw Error('retired callback')};
            x.onreadystatechange=()=>{if(x.readyState===2)location.assign('/next')};x.send();</script>''', 'text/html')
        elif path == '/next':
            self.respond(200, b'<div id="result" class="PASS">next document</div>', 'text/html')
        elif path == '/headers-first':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.flush()
            observed = headers_seen.wait(1.0)
            self.write(b'headers observed' if observed else b'headers hidden until body')
        elif path == '/ack-headers':
            headers_seen.set()
            self.respond(204, b'')
        elif path == '/stream':
            self.send_response(200, 'Read Me')
            self.send_header('Content-Type', 'text/plain; charset=utf-8')
            self.send_header('Content-Length', '14')
            self.send_header('X-Mixed', 'one')
            self.send_header('X-Mixed', 'two')
            self.send_header('Set-Cookie', 'xhr_cookie=present; Path=/')
            self.end_headers()
            self.write(b'Salut ')
            time.sleep(.12)
            self.write('România'.encode())
        elif path in ('/redirect', '/cross', '/loop'):
            self.send_response(302)
            self.send_header('Location', '/ok' if path == '/redirect' else '/loop' if path == '/loop'
                             else f'http://localhost:{self.server.server_port}/forbidden')
            self.send_header('Content-Length', '0')
            self.end_headers()
        elif path.startswith('/status/'):
            code = int(path.rsplit('/', 1)[1])
            self.respond(code, b'' if code == 304 else f'status {code}'.encode())
        elif path == '/retired':
            # Navigation starts from HEADERS_RECEIVED, proving the request is
            # actually in flight rather than racing a fixed-duration JS timer.
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', '8')
            self.end_headers()
            self.wfile.flush()
            time.sleep(.3)
            self.write(b'old body')
        elif path in ('/slow', '/abort', '/replaced'):
            time.sleep(.3)
            self.respond(200, b'old body')
        elif path == '/latin':
            self.respond(200, b'caf\xe9 \x80', 'text/plain; charset=iso-8859-1')
        elif path == '/bom':
            self.respond(200, b'\xff\xfe'+ '雪'.encode('utf-16le'), 'text/plain; charset=windows-1252')
        elif path == '/bad-prefix':
            self.respond(200, b'a\xe2\x82b', 'text/plain; charset=utf-8')
        elif path == '/bad-utf16':
            self.respond(200, b'\xff\xfe\x00\xd8A\x00', 'text/plain; charset=utf-16')
        elif path == '/invalid':
            self.respond(200, b'a\xffb', 'text/plain; charset=utf-8')
        else:
            self.respond(200, b'body')

    def write(self, body):
        try:
            self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def respond(self, code, body, mime='text/plain'):
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1', QT_FONT_DPI='96',
           ASAN_OPTIONS='detect_leaks=0', NO_PROXY='127.0.0.1,localhost')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-xhr-') as tmp:
        for case in ('test', 'navigate'):
            requests.clear()
            dest = Path(tmp) / case
            process = subprocess.run([str(binary), '-v', '--enable_javascript=1', '--js_trace=1',
                '--capture-dir', str(dest), '--capture-size', '800x600', '--capture-id', 'result',
                '--capture-settle', '2500', '--capture-timeout', '6000',
                f'http://127.0.0.1:{server.server_port}/{case}'], env=env, capture_output=True, text=True, timeout=12)
            log = process.stdout + process.stderr
            assert process.returncode == 0 and 'runtime error:' not in log, log[-12000:]
            meta = json.loads((dest / 'capture.json').read_text())
            dom = (dest / 'dom.txt').read_text()
            diagnostic = '\n'.join(l for l in log.splitlines() if 'JavaScript ' in l or 'Console:' in l or '[JS ' in l)
            assert meta['result'] == 'complete' and meta['load_complete'], (meta, diagnostic)
            assert 'class="PASS"' in dom and 'XHR_FAIL:' not in log, (case, dom, diagnostic[-15000:], requests)
            paths = [p for p, _ in requests]
            if case == 'test':
                assert '/forbidden' not in paths and '/must-not-fetch' not in paths, requests
                headers = next(h for p, h in requests if p == '/stream')
                lower = {k.lower(): v for k, v in headers.items()}
                assert lower['x-probe'] == 'first, second' and lower['x-empty'] == '', headers
                assert lower['host'].startswith('127.0.0.1:') and 'forged' not in lower.get('cookie', ''), headers
            else:
                assert meta['content_url'].endswith('/next') and '/retired' in paths, (meta, requests)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print('Async XHR HTTP states, events, headers, decoding, redirects, origin isolation, abort, timeout and retirement pass.')
