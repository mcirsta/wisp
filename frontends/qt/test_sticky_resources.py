#!/usr/bin/env python3
"""A newly revealed image must refresh sticky constraints after decoding."""
import http.server
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

binary = str(Path(sys.argv[1]).resolve())
fixture = Path(__file__).resolve().parents[2] / 'test/sticky-lifecycle.html'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96')
source = fixture.read_text().replace('</style>', '''
#body {height:600px}
#grow:checked ~ #container #body {height:auto}
#late {display:none}
#grow:checked ~ #container #late {display:block}
</style>''').replace('<div id="body"></div>',
    '<div id="body"><div style="height:600px"></div><img id="late" src="/late.png"></div>')


def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))


png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', 80, 800, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress((b'\0' + bytes((34, 85, 204)) * 80) * 800)) + chunk(b'IEND', b''))


class Handler(http.server.BaseHTTPRequestHandler):
    requests = 0

    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == '/late.png':
            Handler.requests += 1
            time.sleep(0.3)
            body, mime = png, 'image/png'
        else:
            body, mime = source.encode(), 'text/html'
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


with tempfile.TemporaryDirectory(prefix='wisp-sticky-image-') as directory:
    tmp = Path(directory)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        snapshots = []
        for name, flags in (('retained', ()), ('full', ('--capture-force-full-restyle',))):
            out = tmp / name
            args = [binary, '--capture-dir', str(out), '--capture-size', '800x600', '--capture-settle', '50',
                    '--capture-timeout', '15000', '--capture-id', 'header', '--capture-id', 'late',
                    '--capture-scroll', '0,900', '--capture-click-label', 'grow', *flags,
                    f'http://127.0.0.1:{server.server_port}/page']
            p = subprocess.run(args, env=env, capture_output=True, text=True, timeout=25)
            assert p.returncode == 0 and 'runtime error:' not in p.stderr, (name, p.stderr)
            meta = json.loads((out / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], meta
            steps = json.loads((out / 'steps.json').read_text())
            def box(step, key):
                e = next(e for e in step['elements'] if e['id'] == key)
                assert e['status'] == 'ok' and e['boxes'], e
                return e['boxes'][0]
            assert steps[1]['scroll']['y'] == steps[2]['scroll']['y'] == 900, steps
            assert box(steps[1], 'header')['border_box']['y'] == 800
            assert box(steps[2], 'header')['border_box']['y'] == 960
            assert box(steps[2], 'late')['border_box']['height'] == 800
            assert box(steps[2], 'header')['sticky']['y']['containing_size'] == 1680
            assert steps[2]['sticky_layout_valid']
            if steps[2].get('render_profile'):
                assert steps[2]['render_profile']['layout_calls'] >= steps[1]['render_profile']['layout_calls'] + 2, steps[2]['render_profile']
            snapshots.append((box(steps[2], 'header')['sticky'], (out / 'page.png').read_bytes()))
        assert Handler.requests == 2, Handler.requests
        assert snapshots[0] == snapshots[1]
    finally:
        server.shutdown()
        worker.join()
        server.server_close()
print('Late image decoding refreshes stuck geometry in retained and full updates.')
