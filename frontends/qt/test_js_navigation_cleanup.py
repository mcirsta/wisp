#!/usr/bin/env python3
"""A retired document's Promise must not navigate during close/destruction."""
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


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.do_GET()

    def do_GET(self):
        requests.append((self.command, self.path))
        if self.path == "/favicon.ico":
            self.send_error(404)
            return
        body = '<!doctype html><meta charset="utf-8"><title>Navigation cleanup</title>'
        if self.path == "/old":
            # Wisp currently leaves Promise jobs queued during normal execution.
            # Retiring the document must discard them, not flush them here.
            body += (f'<script>Promise.resolve().then(function() {{ '
                     f'location.replace("{origin}/unexpected"); }});</script>'
                     '<form action="/next" method="post"><button id="accept">Accept</button></form>')
        else:
            body += '<p id="destination">Expected destination</p>'
        body = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f"http://127.0.0.1:{server.server_port}"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96", NO_PROXY="127.0.0.1")
try:
    with tempfile.TemporaryDirectory(prefix="wisp-js-cleanup-") as tmp:
        for mode in ("navigate", "close"):
            requests.clear()
            dest = Path(tmp) / mode
            args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=1"]
            if mode == "navigate":
                args += ["--capture-click-id", "accept", "--capture-id", "destination"]
            result = subprocess.run([*args, origin + "/old"], env=env, capture_output=True, text=True, timeout=12)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (mode, result.stderr)
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (mode, meta)
            assert meta["content_url"] == origin + ("/next" if mode == "navigate" else "/old"), meta
            assert not any(path == "/unexpected" for _, path in requests), (mode, requests)
            if mode == "navigate":
                assert ("POST", "/next") in requests, requests
                info = json.loads((dest / "inspection.json").read_text())
                assert info["elements"][0]["status"] == "ok", info["elements"]
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print("Queued JS callbacks do not run during form navigation or window shutdown.")
