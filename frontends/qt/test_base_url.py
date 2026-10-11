#!/usr/bin/env python3
"""Parser-time HTML base selection affects real resources and native submission."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
requests = []
cases = {
    'root-relative': ('<base href="/assets/">', '/assets/'),
    'relative': ('<base href="assets/">', '/dir/assets/'),
    'parent': ('<base href="../assets/">', '/assets/'),
    'absolute': ('<base href="__ORIGIN__/assets/">', '/assets/'),
    'first-wins': ('<base href="/first/"><base href="/second/">', '/first/'),
    'target-only': ('<base target="_self"><base href="/assets/">', '/assets/'),
    'empty-first': ('<base href=""><base href="/ignored/">', '/dir/'),
    'data-first': ('<base href="data:text/plain,ignored"><base href="/ignored/">', '/dir/'),
    'script-first': ('<base href="javascript:void(0)"><base href="/ignored/">', '/dir/'),
    'no-base': ('', '/dir/'),
}


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, body, kind):
        data = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        requests.append(self.path)
        if self.path == '/favicon.ico':
            self.send_error(404)
        elif self.path.startswith('/dir/page?'):
            name = self.path.split('?', 1)[1]
            bases, _expected = cases[name]
            bases = bases.replace('__ORIGIN__', origin)
            self.respond('<!doctype html><head>' + bases +
                         '<link rel="stylesheet" href="page.css"><script src="page.js"></script></head>'
                         '<body><p id="marker">Content</p><img src="pixel.svg" width="10" height="10">'
                         '<form action="send"><input type="hidden" name="k" value="v">'
                         '<button id="send">Send</button></form></body>', 'text/html; charset=utf-8')
        elif self.path.endswith('/page.css'):
            self.respond('#marker { width:200px;height:37px;background:#cfc; }', 'text/css')
        elif self.path.endswith('/page.js'):
            self.respond('console.log("BASE_SCRIPT_PASS");', 'text/javascript')
        elif self.path.endswith('/pixel.svg'):
            self.respond('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">'
                         '<rect width="10" height="10" fill="green"/></svg>', 'image/svg+xml')
        else:
            self.respond('<!doctype html><p>Submitted</p>', 'text/html; charset=utf-8')


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-base-url-') as tmp:
        # Static behavior must not depend on JS; repeat relative and multiple
        # bases with a parser-blocking script to check downstream script fetches.
        runs = [(name, False) for name in cases] + [('relative', True), ('first-wins', True)]
        for name, js in runs:
            requests.clear()
            prefix = cases[name][1]
            dest = Path(tmp) / (name + str(js))
            args = [str(binary), '-v', '--capture-dir', str(dest), '--capture-size', '500x350',
                    '--capture-settle', '100', '--capture-timeout', '8000',
                    f'--enable_javascript={int(js)}', '--capture-click-id', 'send',
                    '--capture-id', 'marker', f'{origin}/dir/page?{name}']
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            assert result.returncode == 0 and 'runtime error:' not in log, (name, js, result.returncode, log)
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (name, js, meta)
            assert meta['content_url'] == origin + prefix + 'send?k=v', (name, js, meta, requests)
            for suffix in ('page.css', 'pixel.svg', 'page.js') if js else ('page.css', 'pixel.svg'):
                assert prefix + suffix in requests, (name, js, suffix, requests)
                assert not any(path.endswith('/' + suffix) and path != prefix + suffix for path in requests), requests
            if js:
                assert 'Console: BASE_SCRIPT_PASS' in log, (name, log[-10000:])
            assert 'JavaScript exception in' not in log, (name, log[-10000:])
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('HTML base URLs: relative/absolute, first href, disallowed-scheme fallback, CSS/images/scripts and forms pass.')
