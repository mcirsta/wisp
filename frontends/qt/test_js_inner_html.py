#!/usr/bin/env python3
"""HTML fragment edits reach pixels, while fragment scripts/templates stay inert."""
import http.server
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
requests = []
prefix = '''<!doctype html><meta charset="utf-8"><title>innerHTML</title>
<style>body{margin:0;font:20px/30px monospace}template{display:block}
#article{width:300px}table{border-spacing:0}td{padding:0 10px}
svg,img{display:block}</style>'''
initial = '<p id="old">Before</p>'
visible = ('<section id="article"><style>#article{background:#20a060}</style>Changed ș<table><tr><td>A</td><td>B</td></tr></table>'
           '<svg width="80" height="25"><rect width="80" height="25" fill="blue"/></svg>'
           '<img src="/image.svg" width="60" height="20"></section>')
scripts = '<script>console.log("FRAGMENT_EXECUTED");</script><script src="/forbidden.js"></script>'
inert = '<p id="inert">Hidden</p><img src="/inert.svg">' + scripts
detached = '<section><style>body{display:none}</style><link rel="stylesheet" href="/detached.css"></section>' + scripts
script = '''const target=document.getElementById('target'), old=target.firstChild, live=target.children;
const template=document.getElementById('template'), detached=document.createElement('div');
function change(){
    template.innerHTML=INERT;
    detached.innerHTML=DETACHED;
    target.innerHTML=MARKUP;
    if(old.parentNode!==null||old.isConnected||live[0]!==document.getElementById('article')||
       document.getElementById('inert')!==null||template.firstChild!==null||
       template.innerHTML.indexOf('Hidden')<0)throw Error('replacement boundaries');
    console.log('INNER_HTML_CHANGED');
}
CALL
'''.replace('INERT', json.dumps(inert)).replace('DETACHED', json.dumps(detached)).replace('MARKUP', json.dumps(visible + scripts))
# Keep closing tags inside JavaScript strings from ending the outer script.
script = script.replace('</', '<\\/')


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        content_type = 'text/html; charset=utf-8'
        if self.path == '/image.svg':
            body = b'<svg xmlns="http://www.w3.org/2000/svg" width="60" height="20"><rect width="60" height="20" fill="orange"/></svg>'
            content_type = 'image/svg+xml'
        elif self.path in ('/reference', '/inline', '/timer', '/disabled'):
            contents = visible if self.path == '/reference' else initial
            markup_script = script
            page_prefix = prefix
            # Late stylesheet activation needs the separate stylesheet-lifecycle
            # work recorded in plans/js_migration_analysis.md. Keep this timer
            # variant about the DOM rebuild and its newly loaded images.
            if self.path == '/timer':
                page_prefix += '<style>#article{background:#20a060}</style>'
                markup_script = script.replace('<style>#article{background:#20a060}<\\/style>', '')
            page = page_prefix + '<main id="target">' + contents + '</main><template id="template"></template>'
            if self.path != '/reference':
                call = 'setTimeout(change,150);' if self.path == '/timer' else 'change();'
                page += '<script>' + markup_script.replace('CALL', call) + '</script>'
            body = page.encode()
        else:
            body = b'Unexpected resource'
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-inner-html-') as tmp:
        reference = None
        for case in ('reference', 'inline', 'timer', 'disabled'):
            requests.clear()
            dest = Path(tmp) / case
            enabled = case in ('inline', 'timer')
            result = subprocess.run([
                str(binary), '-v', '--js_trace=1', f'--enable_javascript={int(enabled)}',
                '--capture-dir', str(dest), '--capture-size', '500x300',
                '--capture-settle', '500', '--capture-timeout', '8000',
                '--capture-id', 'article', '--capture-id', 'inert',
                f'http://127.0.0.1:{server.server_port}/{case}',
            ], env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            assert result.returncode == 0 and 'runtime error:' not in log, (case, log[-10000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, log[-10000:])
            elements = json.loads((dest / 'inspection.json').read_text())['elements']
            assert elements[1]['status'] == 'not_found', (case, elements)
            assert '/forbidden.js' not in requests and '/inert.svg' not in requests and '/detached.css' not in requests, (case, requests)
            assert 'Console: FRAGMENT_EXECUTED' not in log and 'JavaScript exception' not in log, (case, log[-10000:])
            assert ('Console: INNER_HTML_CHANGED' in log) == enabled, (case, log[-10000:])
            pixels = (dest / 'page.png').read_bytes()
            if case == 'reference':
                reference = pixels
            elif enabled:
                assert elements[0]['status'] == 'ok' and elements[0]['boxes'], (case, elements)
                assert pixels == reference, (case, elements, log[-10000:])
                changes = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] dom.mutation .*operation=innerHTML .*result=0', log)
                renders = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] render.request .*changes=1 mode=(\S+)', log)
                assert len(changes) == 3 and len(renders) == 1, (case, log[-10000:])
                assert all(change == renders[0][:2] for change in changes), (case, changes, renders)
                assert renders[0][2] == ('initial-build' if case == 'inline' else 'full-restyle'), renders
            else:
                assert pixels != reference and elements[0]['status'] == 'not_found', (case, elements)
finally:
    server.shutdown()
    thread.join()
    server.server_close()
