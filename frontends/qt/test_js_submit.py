#!/usr/bin/env python3
"""Real Qt clicks/implicit submission -> SubmitEvent -> observed HTTP requests."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
received = []
cases = {
    'normal': ("", [('GET', '/send?q=old&go=yes', '')]),
    'cancel': ("e.preventDefault();", []),
    'microtask': ("queueMicrotask(()=>{e.preventDefault();});", []),
    'edit': ("field.setAttribute('value','new');button.setAttribute('value','edited');"
             "button.setAttribute('formaction','/edited');button.setAttribute('formmethod','post');",
             [('POST', '/edited', 'q=new&go=edited')]),
    'remove-form': ("form.parentNode.removeChild(form);", []),
    'remove-button': ("button.parentNode.removeChild(button);", [('GET', '/?q=old', '')]),
    'move-button': ("other.appendChild(button);", [('GET', '/other?q=old', '')]),
    'disable-button': ("button.setAttribute('disabled','');", [('GET', '/send?q=old', '')]),
    'change-type': ("button.setAttribute('type','button');button.setAttribute('formaction','/ignored');",
                    [('GET', '/send?q=old', '')]),
    'exception': ("throw Error('expected-submit-exception');", [('GET', '/send?q=old&go=yes', '')]),
    'timeout': ("while(true){}", []),
    'navigate': ("e.preventDefault();location.assign('/navigated');", [('GET', '/navigated', '')]),
    'replace': ("e.preventDefault();location.replace('navigated');", [('GET', '/navigated', '')]),
    'base': ("e.preventDefault();location.assign('navigated');", [('GET', '/base/navigated', '')]),
    'twice': ("e.preventDefault();", []),
    'implicit': ("", [('GET', '/send?q=old&go=yes', '')]),
    'implicit-no-button': ("", [('GET', '/send?q=old', '')]),
    'no-js': ("throw Error('disabled JS ran');", [('GET', '/send?q=old&go=yes', '')]),
}


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, body):
        data = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        data = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        received.append(('POST', self.path, data))
        self.respond('<!doctype html><p>Submitted</p>')

    def do_GET(self):
        if self.path == '/favicon.ico':
            self.send_error(404)
            return
        # Both initial and post-submission document URLs are observed. Removing
        # the submitter leaves no owner to inherit action from, hence /?q=old.
        if self.path != '/':
            received.append(('GET', self.path, ''))
            self.respond('<!doctype html><p>Submitted</p>')
            return
        body, _expected = cases[self.server.case]
        no_button = self.server.case == 'implicit-no-button'
        base = '<base href="/base/">' if self.server.case == 'base' else ''
        self.respond('<!doctype html>' + base + '''<style>body {font:20px/30px monospace;}
            input,button {display:block;width:160px;height:40px;margin:10px;}</style>
            <form id="form" action="/send" novalidate><input id="field" name="q" value="old">''' +
            ('' if no_button else '<button id="send" name="go" value="yes">Send</button>') + '''</form>
            <form id="other" action="/other"><input name="z" value="two"></form><script>
            var form=document.getElementById('form'),field=document.getElementById('field'),
                button=document.getElementById('send'),other=document.getElementById('other'),
                seen=[],calls=0,saved;
            document.addEventListener('click',e=>{if(e.target===button)seen.push('click');});
            window.addEventListener('submit',e=>seen.push('capture'),true);
            form.addEventListener('submit',e=>{
                if(!(e instanceof SubmitEvent)||!e.isTrusted||!e.bubbles||!e.cancelable||e.composed||
                   e.target!==form||e.currentTarget!==form||e.submitter!==button)
                    throw Error('submit payload');
                if(button&&seen.join()!=='click,capture')throw Error('submit order '+seen);
                if(!button&&seen.join()!=='capture')throw Error('implicit order '+seen);
                saved=e;calls++;seen.push('submit');console.log('SUBMIT_PASS '+calls);
                ''' + body + '''
            });
            document.addEventListener('submit',e=>{
                if(e!==saved||e.submitter!==button)throw Error('submit identity');
                seen=[];console.log('BUBBLE_PASS');
            });</script>''')


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f'http://127.0.0.1:{server.server_port}'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_SCALE_FACTOR='1',
           QT_SCREEN_SCALE_FACTORS='1', QT_FONT_DPI='96', NO_PROXY='127.0.0.1')
try:
    with tempfile.TemporaryDirectory(prefix='wisp-js-submit-') as tmp:
        for name, (_body, expected) in cases.items():
            server.case = name
            received.clear()
            dest = Path(tmp) / name
            args = [str(binary), '-v', '--js_trace=1', '--script_timeout=1',
                    '--capture-dir', str(dest), '--capture-size', '500x350',
                    '--capture-settle', '100', '--capture-timeout', '8000',
                    f'--enable_javascript={int(name != "no-js")}']
            if name.startswith('implicit'):
                args += ['--capture-click-id', 'field', '--capture-type', '\r']
            else:
                args += ['--capture-click-id', 'send']
                if name == 'twice':
                    args += ['--capture-click-id', 'send']
            result = subprocess.run([*args, origin + '/'], env=env, capture_output=True, text=True, timeout=12)
            log = result.stdout + result.stderr
            assert result.returncode == 0 and 'runtime error:' not in log, (name, log[-10000:])
            meta = json.loads((dest / 'capture.json').read_text())
            assert meta['result'] == 'complete' and meta['load_complete'], (name, meta, log[-10000:])
            diagnostic = '\n'.join(line for line in log.splitlines()
                                   if '[JS ' in line or 'JavaScript ' in line or 'Console:' in line)
            assert received == expected, (name, received, diagnostic)
            if name == 'no-js':
                assert 'SUBMIT_PASS' not in diagnostic, diagnostic
                continue
            assert 'form.dispatch type=submit origin=browser' in diagnostic, (name, diagnostic)
            assert 'Console: SUBMIT_PASS 1' in diagnostic, (name, diagnostic)
            if name != 'timeout':
                assert 'Console: BUBBLE_PASS' in diagnostic, (name, diagnostic)
            if name == 'twice':
                assert 'Console: SUBMIT_PASS 2' in diagnostic, diagnostic
            errors = [line for line in diagnostic.splitlines() if 'JavaScript message:' in line]
            if name == 'exception':
                assert errors and all('expected-submit-exception' in line for line in errors), diagnostic
            elif name == 'timeout':
                assert 'interrupted' in diagnostic, diagnostic
            else:
                assert not errors and 'JavaScript exception in' not in diagnostic, (name, diagnostic)
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print('Qt submissions: native payload, ordering, cancellation, mutation, lifetime, limits and HTTP requests pass.')
