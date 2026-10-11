#!/usr/bin/env python3
"""HTML classic-script ordering and CSS blocking, with real fetches and checkpoints.

Expectations follow HTML's parser-blocking/async/defer algorithms. Console markers
observe execution order independently of rendering and DOM mutation.
"""
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
requests, completed = [], []

ORDER = '''<!doctype html><meta charset="utf-8"><title>Script order</title>
<script>function mark(s) { console.log('SCRIPT_ORDER:' + s); } mark('start');</script>
<script src="/first.js"></script>
<script>mark('after-first:' + first);</script>
<script src="/same.js"></script><script src="/same.js"></script>
<script src="/missing.js"></script><script>mark('after-failure');</script>
<script defer src="/defer-slow.js"></script><script defer src="/defer-missing.js"></script>
<script defer src="/defer-fast.js"></script>
<script type="module" src="/unsupported.js"></script>
<script type="application/json" src="/unsupported.js"></script>
<script type="application/json">mark('data-block-executed');</script>
<script type="">mark('inline-end');
Promise.reject(new Error('EXPECTED_UNHANDLED'));
var rejection = Promise.reject(new Error('HANDLED_SAME_CHECKPOINT'));
queueMicrotask(() => rejection.catch(() => {}));
</script><p id="done">Script order fixture</p>'''

CSS = '''<!doctype html><meta charset="utf-8"><title>Stylesheet blocking</title>
<script>function mark(s) { console.log('SCRIPT_ORDER:' + s); } var asynchronous = false;</script>
<script async defer src="/async-slow.js"></script>
<script async="false" src="/async.js"></script>
<link rel="stylesheet" href="/slow.css">
<style>@import url('/import.css'); p { color: green; }</style>
<script>mark('after-css:' + asynchronous);</script>
<script src="/after-css.js"></script><p id="done">Stylesheet fixture</p>'''


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        code, mime, delay = 200, 'text/javascript', 0
        if self.path in ('/order', '/disabled'):
            body, mime = ORDER, 'text/html'
        elif self.path == '/css':
            body, mime = CSS, 'text/html'
        elif self.path == '/first.js':
            body, delay = "var first = 42; mark('first'); Promise.resolve().then(() => mark('first-job'));", .1
        elif self.path == '/same.js':
            body = "var repeated = (typeof repeated === 'undefined' ? 0 : repeated) + 1; mark('same:' + repeated);"
        elif self.path == '/defer-slow.js':
            body, delay = "mark('defer-slow'); queueMicrotask(() => mark('defer-job'));", .3
        elif self.path == '/defer-fast.js':
            body = "mark('defer-fast');"
        elif self.path == '/async.js':
            body = "asynchronous = true; mark('async-fast');"
        elif self.path == '/async-slow.js':
            body, delay = "mark('async-slow');", .2
        elif self.path in ('/slow.css', '/import.css'):
            body, mime, delay = 'p { background: #eee; }', 'text/css', .5
        elif self.path == '/after-css.js':
            # A parser-blocking inline script preceding this fetch must wait
            # for both earlier stylesheets, including imported dependencies.
            ready = '/slow.css' in completed and '/import.css' in completed
            body = f"mark('css-complete:{str(ready).lower()}');"
        else:
            body, code = 'missing', 404
        time.sleep(delay)
        body = body.encode()
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Cache-Control', 'public, max-age=3600')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        completed.append(self.path)
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-script-order-') as tmp:
        for case, expected in (
            ('order', ['start', 'first', 'first-job', 'after-first:42', 'same:1', 'same:2',
                       'after-failure', 'inline-end', 'defer-slow', 'defer-job', 'defer-fast']),
            ('css', ['async-fast', 'async-slow', 'after-css:true', 'css-complete:true']),
            ('disabled', []),
        ):
            requests.clear()
            completed.clear()
            dest = Path(tmp) / case
            result = subprocess.run([
                str(binary), '-v', f'--enable_javascript={0 if case == "disabled" else 1}',
                '--capture-dir', str(dest),
                '--capture-size', '640x480', '--capture-settle', '50', '--capture-timeout', '8000',
                '--capture-id', 'done', origin + '/' + case,
            ], env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            diagnostic = '\n'.join(line for line in log.splitlines()
                                   if 'JavaScript ' in line or 'Console: ' in line or 'Sanitizer' in line)
            assert result.returncode == 0 and 'runtime error:' not in log, (case, diagnostic, log[-5000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, diagnostic)
            info = json.loads((dest / 'inspection.json').read_text())
            assert info['elements'][0]['status'] == 'ok', (case, info)
            actual = re.findall(r'Console: SCRIPT_ORDER:([^\r\n]+)', log)
            assert actual == expected, (case, actual, expected, diagnostic)
            assert 'JavaScript exception' not in diagnostic, diagnostic
            assert '/unsupported.js' not in requests, requests
            if case == 'disabled':
                assert not any(path.endswith('.js') for path in requests), requests
            if case == 'order':
                assert diagnostic.count('JavaScript unhandled Promise rejection') == 1, diagnostic
                assert 'JavaScript message: EXPECTED_UNHANDLED' in diagnostic, diagnostic
                assert 'JavaScript message: HANDLED_SAME_CHECKPOINT' not in diagnostic, diagnostic
                assert requests.count('/same.js') == 1, ('cached script was fetched again', requests)
                assert completed.index('/defer-fast.js') < completed.index('/defer-slow.js'), completed
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('Classic script order, cache hits, fetch failure, async/defer and CSS blocking pass.')
