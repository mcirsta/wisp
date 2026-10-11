#!/usr/bin/env python3
"""Parsed template contents are inert, with scripting both enabled and disabled."""
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
prefix = '''<!doctype html><meta charset="utf-8"><title>Templates</title>
<style>body{margin:0;font:20px/30px monospace}template{display:block}
#visible{height:60px;background:#20a060}</style>'''
visible = '<div id="visible">Ordinary document content</div>'
template = '''<template id="sample"><div id="inert">Hidden content</div>
<style>#visible{height:200px;background:red}</style>
<link rel="stylesheet" href="/inert.css"><base href="/inert-base/">
<script>console.log('TEMPLATE_EXECUTED');</script><script src="/inert.js"></script>
<img src="/inert.png"><iframe src="/inert-frame"></iframe>
<svg width="200" height="200"><rect width="200" height="200" fill="red"/></svg>
<template><script>console.log('TEMPLATE_EXECUTED');</script></template></template>'''
check = '''<script>
const host = document.getElementById('sample');
if (!host || host.firstChild !== null || host.textContent !== '' ||
    document.getElementById('inert') !== null || document.getElementsByTagName('img').length !== 0)
    throw Error('template contents leaked into document');
console.log('TEMPLATE_ISOLATED');
</script>'''


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        if self.path in ('/reference', '/enabled', '/disabled'):
            body = (prefix + visible + ('' if self.path == '/reference' else template + check)).encode()
            status = 200
        else:
            body, status = b'Unexpected resource', 404
        self.send_response(status)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-templates-') as tmp:
        reference = None
        for case in ('reference', 'enabled', 'disabled'):
            requests.clear()
            dest = Path(tmp) / case
            result = subprocess.run([
                str(binary), '-v', '--js_trace=1', f'--enable_javascript={int(case == "enabled")}',
                '--capture-dir', str(dest), '--capture-size', '500x300',
                '--capture-settle', '100', '--capture-timeout', '6000',
                '--capture-id', 'visible', '--capture-id', 'inert',
                f'http://127.0.0.1:{server.server_port}/{case}',
            ], env=env, capture_output=True, text=True, timeout=10)
            log = result.stdout + result.stderr
            assert result.returncode == 0 and 'runtime error:' not in log, (case, log[-8000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, log[-8000:])
            elements = json.loads((dest / 'inspection.json').read_text())['elements']
            assert elements[0]['status'] == 'ok' and elements[0]['boxes'], (case, elements)
            assert elements[1]['status'] == 'not_found', (case, elements)
            assert not any(path.startswith('/inert') for path in requests), (case, requests)
            assert 'Console: TEMPLATE_EXECUTED' not in log and 'JavaScript exception' not in log, (case, log[-8000:])
            assert ('Console: TEMPLATE_ISOLATED' in log) == (case == 'enabled'), (case, log[-8000:])
            pixels = (dest / 'page.png').read_bytes()
            if reference is None:
                reference = pixels
            else:
                assert pixels == reference, (case, elements)
finally:
    server.shutdown()
    thread.join()
    server.server_close()
