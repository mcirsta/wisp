#!/usr/bin/env python3
"""Check Promise navigation, shutdown, and recovery without running aborted jobs."""
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
        if self.path in ("/old", "/runaway"):
            body += '<script>Promise.resolve().then(() => console.log("JS_PROMISE_RAN"));</script>'
            if self.path == "/runaway":
                # An interrupted entry discards its jobs. Later entries have
                # working checkpoints, including after navigating to /next.
                body += (f'<script>Promise.resolve().then(() => location.replace("{origin}/unexpected"));'
                         'var caught = false, finished = false; '
                         'try { while (true) {} } catch (e) { caught = true; } '
                         'finally { finished = true; } '
                         'console.log("JS_TIMEOUT_UNREACHABLE");</script>'
                         '<script>if (caught || finished) throw new Error("interruption was caught"); '
                         'Promise.resolve().then(() => console.log("JS_TIMEOUT_RECOVERY"));</script>')
            body += '<form action="/next" method="post"><button id="accept">Accept</button></form>'
        elif self.path == "/event-navigation":
            body += ('<div id="source">Event navigation</div><script>'
                     'var source=document.getElementById("source");'
                     'var link=document.createElement("link");link.setAttribute("rel","stylesheet");'
                     f'link.setAttribute("href","{origin}/unexpected");'
                     'link.dispatchEvent(new Event("DOMNodeInserted"));'
                     f'document.addEventListener("leave",()=>location.replace("{origin}/next"));'
                     'setTimeout(()=>source.dispatchEvent(new Event("leave",{bubbles:true})),50);</script>')
        elif self.path == "/microtask-navigation":
            body += (f'<script>Promise.resolve().then(() => location.assign("{origin}/next"));</script>'
                     '<p>Navigation from a Promise</p>')
        else:
            body += '<script>queueMicrotask(() => console.log("JS_DESTINATION_JOB"));</script>'
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
        for mode in ("navigate", "close", "timeout", "microtask-navigation", "event-navigation"):
            requests.clear()
            dest = Path(tmp) / mode
            args = [str(binary), "-v", "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=1"]
            navigate = mode != "close"
            if mode in ("navigate", "timeout"):
                args += ["--capture-click-id", "accept", "--capture-id", "destination"]
            path = "/runaway" if mode == "timeout" else (
                "/" + mode if mode in ("microtask-navigation", "event-navigation") else "/old")
            if mode == "timeout":
                args[2:2] = ["--script_timeout=1", "--verbose_filter=level:INFO"]
            result = subprocess.run([*args, origin + path], env=env, capture_output=True, text=True, timeout=12)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (mode, result.stdout, result.stderr)
            if mode in ("microtask-navigation", "event-navigation"):
                assert "Forcing content cleanup during shutdown" not in result.stdout + result.stderr, result.stderr
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (mode, meta)
            assert meta["content_url"] == origin + ("/next" if navigate else path), meta
            assert not any(path == "/unexpected" for _, path in requests), (mode, requests)
            log = "\n".join(line for line in (result.stdout + result.stderr).splitlines()
                            if "JavaScript " in line or "Console: " in line)
            if mode not in ("microtask-navigation", "event-navigation"):
                assert "Console: JS_PROMISE_RAN" in log, log
            if mode == "timeout":
                assert "JavaScript execution limit reached in " + origin + path in log, log
                assert "JavaScript stack:" in log, log
                assert "Console: JS_TIMEOUT_RECOVERY" in log, log
                assert "Console: JS_TIMEOUT_UNREACHABLE" not in log, log
            if navigate:
                assert "Console: JS_DESTINATION_JOB" in log, log
            if mode in ("navigate", "timeout"):
                assert ("POST", "/next") in requests, requests
                info = json.loads((dest / "inspection.json").read_text())
                assert info["elements"][0]["status"] == "ok", info["elements"]
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print("Promise/event navigation, timeout recovery, form navigation and shutdown pass.")
