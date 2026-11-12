#!/usr/bin/env python3
"""A warm stylesheet cache must not finish a parser while a script resumes it."""
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


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        mime, status = 'text/html', 200
        if self.path == '/':
            body = '''<!doctype html><meta charset="utf-8"><title>Source</title>
                <link rel="stylesheet" href="/shared.css">
                <p><a id="one" href="/article?1">First article</a></p>
                <p><a id="two" href="/redirect"><span>Redirected article</span></a></p>
                <p><a id="blank" href="/article?3" target="_blank">Blank target</a></p>'''
        elif self.path.startswith('/article?'):
            number = self.path.split('?')[1]
            # HTML finishes downloading while this external script blocks the
            # parser. On resume the cached CSS synchronously calls its owner.
            body = f'''<!doctype html><meta charset="utf-8"><title>Article {number}</title>
                <script src="/resume.js?{number}"></script>
                <link rel="stylesheet" href="/shared.css">
                <script>document.title='Pending';document.addEventListener('DOMContentLoaded',function(){{
                    document.title=document.getElementById('tail')?'Article {number}':'Missing tail';
                }});</script><p id="tail">The entire article must be parsed.</p>'''
        elif self.path.startswith('/resume.js?'):
            time.sleep(.2)
            body, mime = '/* parser-blocking resource */', 'text/javascript'
        elif self.path == '/shared.css':
            body, mime = 'body { color: #234; }', 'text/css'
        elif self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/article?2')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        else:
            status, body = 404, 'Missing'
        data = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'public, max-age=3600')
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
try:
    with tempfile.TemporaryDirectory(prefix='wisp-tab-navigation-') as tmp:
        for name, js, dpr, actions in (
            ('middle', 1, 1, [('middle-click-id', 'one'), ('middle-click-id', 'two'), ('click-id', 'blank')]),
            ('middle-hidpi', 1, 2, [('middle-click-id', 'one')]),
            ('same-window', 1, 1, [('click-id', 'one')]),
            ('no-js', 0, 1, [('middle-click-id', 'one')]),
        ):
            requests.clear()
            dest = Path(tmp) / name
            env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR=str(dpr),
                       QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
            args = [str(binary), f'--enable_javascript={js}', '--capture-dir', str(dest),
                    '--capture-size', '640x480', '--capture-settle', '500', '--capture-timeout', '10000']
            for kind, value in actions:
                args += ['--capture-' + kind, value]
            result = subprocess.run([*args, origin + '/'], env=env, capture_output=True, text=True, timeout=15)
            assert result.returncode == 0 and 'runtime error:' not in result.stderr, (name, result.stderr)
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['load_complete'] and meta['result'] == 'complete', (name, meta)
            tabs = meta['icons']['tabs']
            expected = ['Article 1'] if name == 'same-window' else ['Source'] + [f'Article {i + 1}' for i in range(len(actions))]
            assert [t['title'] for t in tabs] == expected, (name, tabs)
            for index, tab in enumerate(tabs):
                assert tab['load_complete'] and not tab['content_url'].startswith('about:'), (name, tab)
                assert tab['current'] == (index == 0), (name, tabs)
            assert requests.count('/shared.css') == 1, ('Stylesheet must come from cache', name, requests)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print('Middle-click, redirects, target=_blank and same-tab navigation with cached CSS and blocked parsing passed.')
