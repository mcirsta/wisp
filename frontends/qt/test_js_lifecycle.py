#!/usr/bin/env python3
"""HTML readiness, resource ordering, lifecycle DOM edits and retirement."""
import base64
import http.server
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

binary = Path(sys.argv[1]).resolve()
completed = []
requests = []
pixel = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a7xkAAAAASUVORK5CYII=')
prefix = '<!doctype html><meta charset="utf-8"><title>Lifecycle</title>'
style = '<style>body{margin:0;font:20px/30px monospace}#result{height:60px;background:#20a060}</style>'
visible = '<div id="result">Ready visible after load</div>'


def page(case):
    if case == 'reference':
        return prefix + style + visible
    script = '''
function mark(s){console.log('LIFECYCLE:'+s+':'+document.readyState);}
function check(e, type){
  if(e.type!==type||!e.isTrusted||e.target!==document||e.cancelable||e.composed)
    throw Error('bad lifecycle event');
}
mark('inline');
document.addEventListener('readystatechange',e=>{check(e,'readystatechange');mark('state');});
document.addEventListener('DOMContentLoaded',e=>{
  check(e,'DOMContentLoaded');mark('dom');
  const result=document.getElementById('result');
  if(!result)throw Error('DOM incomplete');
  result.removeAttribute('hidden');
  result.textContent='Ready visible';
  queueMicrotask(()=>mark('dom-job'));
});
window.addEventListener('DOMContentLoaded',()=>mark('dom-window'));
window.addEventListener('load',e=>{
  check(e,'load');
  if(e.currentTarget!==window||e.bubbles||e.composedPath().length!==1||e.composedPath()[0]!==window)
    throw Error('bad Window load path');
  mark('load');
  document.getElementById('result').textContent+=' after load';
  queueMicrotask(()=>mark('load-job'));
});
document.addEventListener('load',()=>{throw Error('Window load reached Document');});
'''
    extra = ''
    if case in ('css', 'defer-css'):
        extra += '<link rel="stylesheet" href="/slow.css">'
        # Ask the server whether its slow CSS response has completed. This is
        # independent of elapsed-time guesses in the test process.
        script += "document.addEventListener('DOMContentLoaded',()=>location.assign('/css-result'));" if case == 'css' else ''
    if case in ('defer', 'defer-css'):
        extra += '<script defer src="/slow-defer.js"></script><script defer src="/missing.js"></script><script defer src="/fast-defer.js"></script>'
    if case == 'async':
        extra += '<script async src="/async.js"></script>'
    if case in ('frame', 'frame-error'):
        extra += '<iframe src="/' + case + '-child"></iframe>'
    if case == 'image':
        extra += '<img src="/slow.png" width="1" height="1">'
    if case == 'mutate':
        script += '''document.addEventListener('DOMContentLoaded',()=>{
          const image=document.createElement('img');image.setAttribute('src','/slow.png');
          image.setAttribute('width','1');image.setAttribute('height','1');document.body.appendChild(image);
        });'''
    if case in ('exception', 'timeout'):
        # Register before the main DCL listener to test ordinary exception
        # continuation versus bounded interruption of the current dispatch.
        throwing = "throw Error('EXPECTED_LIFECYCLE');" if case == 'exception' else 'while(true){}'
        script = "document.addEventListener('DOMContentLoaded',()=>{" + throwing + "});" + script
    if case.startswith('navigate-'):
        phase = case.removeprefix('navigate-')
        if phase in ('interactive', 'complete'):
            script = ("document.addEventListener('readystatechange',()=>{if(document.readyState==='" + phase +
                      "')location.assign('/destination');});") + script
        else:
            target, event = ('document', 'DOMContentLoaded') if phase == 'dom' else ('window', 'load')
            script = target + ".addEventListener('" + event + "',()=>location.assign('/destination'));" + script
    if case == 'timeout':
        # The interrupted DCL dispatch skips its later listeners. Load must
        # still run in a fresh bounded entry and produce the reference result.
        script += "window.addEventListener('load',()=>{const r=document.getElementById('result');r.removeAttribute('hidden');r.textContent='Ready visible after load';});"
    return prefix + style + '<script>' + script + '</script>' + extra + '<div id="result" hidden>Before</div>'


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        code, mime, delay = 200, 'text/html; charset=utf-8', 0
        if self.path == '/favicon.ico' or self.path == '/missing.js':
            body, code = b'missing', 404
        elif self.path == '/slow.css':
            body, mime, delay = b'body{color:black}', 'text/css', .6
        elif self.path in ('/slow-defer.js', '/fast-defer.js', '/async.js'):
            name = self.path[1:-3]
            body, mime = f"mark('{name}');queueMicrotask(()=>mark('{name}-job'));".encode(), 'text/javascript'
            delay = .3 if name == 'slow-defer' else .6 if name == 'async' else 0
        elif self.path == '/frame-child':
            body = (prefix + '<img src="/slow.png"><script>window.addEventListener("load",()=>console.log("FRAME_LOADED"));</script>').encode()
        elif self.path == '/frame-error-child':
            body, code = b'missing frame', 404
        elif self.path == '/slow.png':
            body, mime, delay = pixel, 'image/png', .6
        elif self.path == '/css-result':
            early = '/slow.css' not in completed
            body = (prefix + style + visible + f'<script>console.log("CSS_INDEPENDENT:{str(early).lower()}");</script>').encode()
        elif self.path == '/destination':
            body = page('basic').encode()
        else:
            body = page(self.path[1:]).encode()
        time.sleep(delay)
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        completed.append(self.path)
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Lifecycle navigation deliberately retires in-flight fetches.


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-js-lifecycle-') as tmp:
        reference = None
        cases = ('reference', 'basic', 'defer', 'defer-css', 'async', 'image', 'mutate',
                 'frame', 'frame-error', 'exception', 'timeout', 'navigate-interactive', 'navigate-dom',
                 'navigate-complete', 'navigate-load', 'css', 'disabled')
        for case in cases:
            completed.clear()
            requests.clear()
            dest = Path(tmp) / case
            result = subprocess.run([
                str(binary), '-v', '--js_trace=1', '--script_timeout=1',
                f'--enable_javascript={int(case != "disabled")}', '--capture-dir', str(dest),
                '--capture-size', '640x480', '--capture-settle', '50', '--capture-timeout', '8000',
                '--capture-id', 'result', origin + '/' + case,
            ], env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            diagnostic = '\n'.join(line for line in log.splitlines()
                                   if 'JavaScript ' in line or 'Console: ' in line or '[JS ' in line)
            assert result.returncode == 0 and 'runtime error:' not in log, (case, diagnostic, log[-6000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, diagnostic)
            events = re.findall(r'Console: LIFECYCLE:([^\r\n]+)', log)
            if case == 'reference':
                reference = (dest / 'page.png').read_bytes()
                continue
            if case == 'disabled':
                assert not events, events
                continue
            if case.startswith('navigate-'):
                assert meta['content_url'] == origin + '/destination', (case, meta, diagnostic)
                assert 'Forcing content cleanup during shutdown' not in log, diagnostic
            elif case == 'css':
                assert 'Console: CSS_INDEPENDENT:true' in log, diagnostic
            else:
                assert events[0] == 'inline:loading', (case, events)
                assert events.count('state:interactive') == 1 and events.count('state:complete') == 1, (case, events)
                assert events.count('load:complete') == 1 and events[-1] == 'load-job:complete', (case, events)
                if case != 'timeout':
                    assert events.count('dom:interactive') == 1, (case, events)
                    i = events.index('dom:interactive')
                    assert events[i:i+3] == ['dom:interactive', 'dom-job:interactive', 'dom-window:interactive'], (case, events)
                if case.startswith('defer'):
                    assert events.index('state:interactive') < events.index('slow-defer:interactive'), events
                    assert events.index('slow-defer-job:interactive') < events.index('fast-defer:interactive') < events.index('dom:interactive'), events
                if case == 'async':
                    assert events.index('dom:interactive') < events.index('async:interactive') < events.index('state:complete'), events
                if case in ('image', 'mutate'):
                    assert '/slow.png' in completed, (case, completed)
            if case == 'frame':
                assert log.index('Console: FRAME_LOADED') < log.index('Console: LIFECYCLE:load:complete'), diagnostic
            if case not in ('image', 'mutate', 'frame', 'frame-error'):
                assert (dest / 'page.png').read_bytes() == reference, (case, diagnostic)
            if case == 'exception':
                assert 'JavaScript message: EXPECTED_LIFECYCLE' in log, diagnostic
            elif case == 'timeout':
                assert 'JavaScript execution limit reached' in log, diagnostic
            else:
                assert 'JavaScript exception' not in log, (case, diagnostic)
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('Document readiness, deferred/async/resource ordering, lifecycle pixels and navigation pass (17 cases).')
