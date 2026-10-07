#!/usr/bin/env python3
"""Real Qt timer tasks, bounds, navigation, fairness and JS-origin tracing."""
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


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        if self.path == '/favicon.ico':
            self.send_error(404)
            return
        body = '<!doctype html><meta charset="utf-8"><title>Timer test</title><p id="page">Timer fixture</p>'
        if self.path in ('/timers', '/quiet'):
            body += '''<script>
                var order = [], n = 0, arg = {};
                var cancelled = setTimeout(() => console.log('BAD_CANCELLED_TIMER'), 0); clearInterval(cancelled);
                setTimeout(function(a) { 'use strict';
                    if (this !== globalThis || a !== arg) throw Error('callback arguments/receiver');
                    order.push('a'); Promise.resolve().then(() => order.push('job'));
                }, 0, arg);
                setTimeout(() => order.push('b'), 0);
                setTimeout("order.push('string')", 0);
                var interval = setInterval(() => {
                    if (++n === 8) { clearTimeout(interval); console.log('TIMER_ORDER:' + order.join(',')); }
                }, 0);
                setTimeout(() => console.log('BAD_RETIRED_TIMER'), 60000);
            </script>'''
        elif self.path == '/runaway':
            body += '''<script>
                setInterval(() => { console.log('RUNAWAY_TIMER'); while (true) {} }, 0);
                setTimeout(() => Promise.resolve().then(() => console.log('TIMER_RECOVERY')), 0);
            </script>'''
        elif self.path == '/navigate':
            body += f'''<script>
                setTimeout(() => {{ Promise.resolve().then(() => location.assign('{origin}/next')); }}, 0);
                setInterval(() => location.assign('{origin}/unexpected'), 300);
            </script>'''
        elif self.path == '/next':
            body += '<script>setTimeout(() => console.log("DESTINATION_TIMER"), 0);</script>'
        elif self.path == '/fairness':
            # Each timer's Promise schedules a new timer task. HTML nesting is
            # reset in a microtask, so the nesting clamp cannot rescue a frontend
            # which drains newly scheduled callbacks without returning to its UI.
            body += '''<script>
                var rounds = 0;
                function again() { if (++rounds === 20) console.log('TIMER_CHAIN_ACTIVE');
                    queueMicrotask(() => setTimeout(again, 0)); }
                setTimeout(again, 0);
            </script>'''
        body = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-js-timers-') as tmp:
        for case in ('timers', 'quiet', 'runaway', 'navigate', 'fairness'):
            requests.clear()
            dest = Path(tmp) / case
            trace = case not in ('quiet', 'fairness')
            result = subprocess.run([
                str(binary), '-v', '--enable_javascript=1', '--script_timeout=1', f'--js_trace={int(trace)}',
                '--capture-dir', str(dest), '--capture-size', '640x480', '--capture-id', 'page',
                '--capture-settle', '500', '--capture-timeout', '6000', origin + '/' + case,
            ], env=env, capture_output=True, text=True, timeout=9)
            log = result.stdout + result.stderr
            diagnostic = '\n'.join(line for line in log.splitlines()
                                   if '[JS ' in line or 'JavaScript ' in line or 'Console: ' in line)
            assert result.returncode == 0 and 'runtime error:' not in log, (case, diagnostic[-12000:], log[-5000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (case, meta, diagnostic[-5000:])
            info = json.loads((dest / 'inspection.json').read_text())
            assert info['elements'][0]['status'] == 'ok' and info['elements'][0]['boxes'], (case, info)
            assert not any(path == '/unexpected' for path in requests), (case, requests)
            assert 'BAD_' not in diagnostic, diagnostic
            if case in ('timers', 'quiet'):
                assert 'Console: TIMER_ORDER:a,job,b,string' in diagnostic, diagnostic
            elif case == 'runaway':
                assert diagnostic.count('Console: RUNAWAY_TIMER') == 1, diagnostic
                assert 'JavaScript execution limit reached' in diagnostic, diagnostic
                assert 'Console: TIMER_RECOVERY' in diagnostic, diagnostic
                assert 'reason=interrupted' in diagnostic, diagnostic
            elif case == 'navigate':
                assert meta['content_url'] == origin + '/next', meta
                assert 'Console: DESTINATION_TIMER' in diagnostic, diagnostic
                nav = re.search(r'\[JS doc=(\d+) entry=(\d+)\] navigation.request method=assign target=', diagnostic)
                assert nav, diagnostic
                doc, entry = nav.groups()
                assert f'[JS doc={doc} entry={entry}] timer.fire ' in diagnostic, diagnostic
                destination = re.search(r'\[JS doc=(\d+) entry=\d+\] begin kind=script source=' +
                                        re.escape(origin + '/next'), diagnostic)
                assert destination and destination[1] != doc, diagnostic
            elif case == 'fairness':
                assert 'Console: TIMER_CHAIN_ACTIVE' in diagnostic, diagnostic
            if trace:
                events = re.findall(r'\[JS doc=(\d+) entry=(\d+)\] (.*)', diagnostic)
                assert events and any('kind=script source=' + origin in event for _, _, event in events), diagnostic
                fires = [(doc, entry, re.fullmatch(r'timer.fire id=(\d+) scheduled_by=(\d+)', event))
                         for doc, entry, event in events if event.startswith('timer.fire ')]
                assert fires, diagnostic
                for doc, entry, match in fires:
                    assert match, diagnostic
                    ident, parent = match.groups()
                    assert any(d == doc and e == parent and event.startswith('timer.schedule id=' + ident + ' ')
                               for d, e, event in events), (doc, entry, ident, parent, diagnostic)
                    assert any(d == doc and e == entry and event.startswith('end result=')
                               for d, e, event in events), diagnostic
                assert 'document.close ' in diagnostic, diagnostic
            else:
                assert '[JS ' not in log, diagnostic
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('Qt timers, shared limits, navigation, event-loop fairness and JS tracing pass.')
