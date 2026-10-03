#!/usr/bin/env python3
"""Local HTTP/TLS-stall fixtures for production fetch-event tests; no internet."""
import http.server
import json
import os
from pathlib import Path
import socket
import socketserver
import subprocess
import sys
import tempfile
import threading


class HTTP(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def log_message(self, *_args):
        pass

    def do_GET(self):
        try:
            self.respond()
        except (BrokenPipeError, ConnectionResetError):
            pass  # Cancellation and mid-response shutdown are intentional.

    def respond(self):
        path = self.path.split("?", 1)[0]
        if path == "/drop":
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/page")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path == "/page":
            body = (b'<!doctype html><link rel="stylesheet" href="/chain/0.css">'
                    b'<style>#probe{height:40px}</style>'
                    b'<div id="probe">event-driven fetching</div>'
                    b'<img src="data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 '
                    b'width=%222%22 height=%222%22/%3E">')
            mime = "text/html"
        elif path.startswith("/chain/"):
            index = int(path.rsplit("/", 1)[1].split(".")[0])
            body = (f'@import url("/chain/{index+1}.css");' if index < 15 else
                    '#probe { width:321px; margin:0; padding:0; background:#abc; }').encode()
            mime = "text/css"
        else:
            body, mime = b"ready", "text/plain"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", "10000" if path == "/stall" else str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        if path == "/stall":
            # Wait for cancellation, without sending another packet to wake curl.
            self.connection.settimeout(5)
            try:
                while self.connection.recv(4096):
                    pass
            except (TimeoutError, ConnectionResetError):
                pass
            self.close_connection = True


class TLSStall(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5)
        try:
            while self.request.recv(4096):
                pass
        except (TimeoutError, ConnectionResetError):
            pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


def run_monkey(binary, origin):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src/test"))
    import monkey_driver
    steps = [
        {"action": "launch", "launch-options": ["enable_javascript=0"], "language": "en"},
        {"action": "window-new", "tag": "win"},
        {"action": "navigate", "window": "win", "url": origin + "/redirect"},
        {"action": "block", "conditions": [{"window": "win", "status": "complete"}]},
        {"action": "plot-check", "window": "win", "checks": [{"text-contains": "event-driven fetching"}]},
        {"action": "navigate", "window": "win", "url": origin + "/stall"},
        {"action": "block", "conditions": [{"window": "win", "status": "loading"}]},
        {"action": "stop", "window": "win"},
        {"action": "navigate", "window": "win", "url": origin + "/page"},
        {"action": "block", "conditions": [{"window": "win", "status": "complete"}]},
        {"action": "navigate", "window": "win", "url": origin + "/stall"},
        {"action": "block", "conditions": [{"window": "win", "status": "loading"}]},
        {"action": "window-close", "window": "win"},
        {"action": "quit"},
    ]
    monkey_driver.run_test_plan({"monkey": str(binary)}, {
        "title": "Socket readiness, dependency loading and cancellation", "group": "fetch", "timeout": 15,
        "steps": steps})


def main():
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
    tls = Server(("127.0.0.1", 0), TLSStall)
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in (httpd, tls)]
    for thread in threads:
        thread.start()
    origin = f"http://127.0.0.1:{httpd.server_port}"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_FONT_DPI="96",
               QT_SCALE_FACTOR="1", QT_SCREEN_SCALE_FACTORS="1", NO_PROXY="127.0.0.1")
    try:
        if sys.argv[1] == "--monkey":
            run_monkey(Path(sys.argv[2]).resolve(), origin)
            return
        core, browser = map(lambda p: Path(p).resolve(), sys.argv[1:3])
        result = subprocess.run([str(core), origin, f"https://127.0.0.1:{tls.server_address[1]}/"],
                                env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, (result.stdout, result.stderr)
        print(result.stdout.strip())
        with tempfile.TemporaryDirectory(prefix="wisp-fetch-events-") as tmp:
            dest = Path(tmp) / "capture"
            args = [str(browser), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-id", "probe", "--capture-settle", "100", "--capture-timeout", "8000",
                    origin + "/redirect"]
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=12)
            assert result.returncode == 0, result.stderr
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["load_complete"] and meta["result"] == "complete", meta
            assert meta["content_url"] == origin + "/page", meta
            info = json.loads((dest / "inspection.json").read_text())
            probe = next(e for e in info["elements"] if e["id"] == "probe")
            assert probe["boxes"][0]["content"]["width"] == 321, probe
            print("Qt HTTP capture: redirect, 16 dependent stylesheets, inline CSS and data image passed.")
    finally:
        for server in (httpd, tls):
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join()


if __name__ == "__main__":
    main()
