#!/usr/bin/env python3
"""Real navigation events: timing, frame completion and stationary busy cursors."""
import http.server
import json
import os
from pathlib import Path
import queue
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

DELAY = 0.4
DONE = re.compile(r"Done \(([0-9.]+)s\)")
MODE, BINARY = sys.argv[1], str(Path(sys.argv[2]).resolve())
ENV = dict(os.environ, NO_PROXY="127.0.0.1", QT_QPA_PLATFORM="offscreen",
           QT_SCALE_FACTOR="1", QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        try:
            self.respond()
        except (BrokenPipeError, ConnectionResetError):
            pass  # Cancellation and replacement navigation are intentional.

    def respond(self):
        path = self.path.split("?", 1)[0]
        if path in ("/headers", "/slow-child", "/slow.svg", "/slow.css", "/drop", "/redirect"):
            time.sleep(DELAY)
        if path == "/drop":
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/headers")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path == "/favicon.ico":
            self.send_error(404)
            return
        body = '<!doctype html><meta charset="utf-8"><title>Loading test</title>'
        mime = "text/html"
        if path == "/frameset":
            body += '<frameset cols="50%,50%"><frame src="/fast"><frame src="/slow-child"></frameset>'
        elif path in ("/frame", "/nested"):
            child = "/frame" if path == "/nested" else "/slow-child"
            body += f'<iframe src="/fast"></iframe><iframe src="{child}"></iframe>'
        elif path in ("/image", "/crosshair", "/text", "/link", "/wait"):
            cursor = path[1:] if path in ("/crosshair", "/text", "/wait") else "auto"
            body += f'<style>body{{margin:0}} #target{{display:block;width:200px;height:100px;cursor:{cursor}}}</style>'
            body += ('<a id="target" href="javascript:void(0)">link</a>' if path == "/link" else
                     '<div id="target">cursor target</div>')
            body += '<img src="/slow.svg" width="40" height="40">'
        elif path == "/css":
            body += '<link rel="stylesheet" href="/slow.css"><p>styled</p>'
        elif path == "/slow.css":
            body, mime = 'body { color: blue; }', "text/css"
        elif path == "/slow.svg":
            body = '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><rect width="40" height="40"/></svg>'
            mime = "image/svg+xml"
        else:
            body += '<p>ready</p>'
        body = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if path == "/body":
            self.wfile.write(body[:32])
            self.wfile.flush()
            time.sleep(DELAY)
            body = body[32:]
        self.wfile.write(body)


class Monkey:
    def __init__(self, profile):
        options = [f"--{key}={profile}/{name}" for key, name in (
            ("cookie_file", "cookies"), ("cookie_jar", "cookies"),
            ("url_file", "urls"), ("disc_cache_path", "cache"))]
        self.log = open(Path(profile) / "stderr", "w+")
        self.process = subprocess.Popen([BINARY, "--enable_javascript=0", *options],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log,
                                        text=True, bufsize=1, env=ENV)
        self.queue = queue.Queue()
        self.events = []
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        self.wait("GENERIC STARTED")
        self.send("WINDOW NEW")
        self.wait("WINDOW NEW WIN 0")

    def read(self):
        for line in self.process.stdout:
            self.queue.put((time.monotonic(), line.strip()))
        self.queue.put((time.monotonic(), "EOF"))

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def wait(self, text):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                at, line = self.queue.get(timeout=max(.001, deadline - time.monotonic()))
            except queue.Empty:
                break
            self.events.append((at, line))
            if text in line:
                return at
            assert line != "EOF", self.diagnostics()
        raise AssertionError(("Timed out waiting for " + text, self.diagnostics()))

    def diagnostics(self):
        self.log.flush()
        self.log.seek(0)
        return self.events[-40:], self.log.read()[-8000:]

    def navigate(self, path):
        self.send("WINDOW GO 0 " + ORIGIN + path)
        self.wait("START_THROBBER")
        # Drop old navigation events, including the previous page's Stop.
        self.events = [self.events[-1]]

    def barrier(self):
        self.send("WINDOW REDRAW 0")
        self.wait("REDRAW WIN 0 STOP")

    def click(self, x=10, y=20):
        self.send(f"WINDOW CLICK WIN 0 X {x} Y {y} BUTTON LEFT KIND SINGLE")
        self.wait("SET_POINTER")

    def complete(self):
        self.wait("STOP_THROBBER")
        self.barrier()

    def last(self, marker):
        return next(line for _, line in reversed(self.events) if marker in line)

    def check_done(self, minimum):
        done = [(at, DONE.search(line)) for at, line in self.events if DONE.search(line)]
        assert done, self.events
        start = self.events[0][0]
        stop = next(at for at, line in self.events if "STOP_THROBBER" in line)
        elapsed = stop - start
        # All Done messages must describe this navigation, including after hover.
        for at, match in done:
            duration = float(match[1])
            assert at - start >= minimum - .1, self.events
            assert duration >= minimum - .1 and abs(duration - elapsed) < .25, (duration, elapsed, self.events)
        assert sum("START_THROBBER" in line for _, line in self.events) == 1, self.events
        assert sum("STOP_THROBBER" in line for _, line in self.events) == 1, self.events
        assert "PROGRESS" not in self.last("SET_POINTER"), self.events

    def close(self):
        if self.process.poll() is None:
            self.send("QUIT")
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.reader.join(timeout=2)
        self.log.seek(0)
        stderr = self.log.read()
        self.log.close()
        self.process.stdin.close()
        self.process.stdout.close()
        assert self.process.returncode == 0 and "runtime error:" not in stderr, stderr


def run_monkey(profile):
    browser = Monkey(profile)
    try:
        for path in ("headers", "body", "image", "css", "frame", "nested", "frameset", "redirect"):
            browser.navigate("/" + path)
            browser.complete()
            browser.check_done(DELAY * (2 if path == "redirect" else 1))
            # Moving/clicking blank content must not restore the content's old
            # shorter duration, including when input is dispatched to an iframe.
            browser.click(700, 500)
            browser.barrier()
            browser.check_done(DELAY * (2 if path == "redirect" else 1))
        for path, pointer in (("image", "DEFAULT"), ("crosshair", "CROSS"),
                              ("text", "CARET"), ("link", "POINT"), ("wait", "WAIT")):
            browser.navigate("/" + path)
            browser.wait("NEW_CONTENT")
            browser.click()
            assert "PROGRESS" in browser.last("SET_POINTER"), browser.events
            browser.complete()
            browser.check_done(DELAY)
            assert browser.last("SET_POINTER").endswith(pointer), browser.events
        for path in ("headers", "image", "frame"):
            browser.navigate("/" + path)
            if path != "headers":
                browser.wait("NEW_CONTENT")
            browser.send("WINDOW STOP 0")
            browser.complete()
            assert browser.last("SET_STATUS").endswith("Stopped"), browser.events
            assert not any(DONE.search(line) for _, line in browser.events), browser.events
            assert "PROGRESS" not in browser.last("SET_POINTER"), browser.events
            browser.navigate("/fast")
            browser.complete()
            browser.check_done(0)
        # Replacing a pending request resets timing and retires the old load.
        browser.navigate("/headers")
        browser.navigate("/fast")
        browser.complete()
        browser.check_done(0)
        browser.navigate("/drop")
        browser.complete()
        assert "PROGRESS" not in browser.last("SET_POINTER"), browser.events
        assert not DONE.search(browser.last("SET_STATUS")), browser.events
        browser.navigate("/fast")
        browser.complete()
        browser.check_done(0)
    finally:
        browser.close()
    print("Loading events: response/body/resource/frame delays, redirects, "
          "cursor restoration, stop, replacement and failure passed.")


def run_qt(profile):
    for case in ("headers", "body", "image", "frame"):
        dest = Path(profile) / case
        args = [BINARY, "--capture-dir", str(dest), "--capture-size", "800x600",
                "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=0",
                ORIGIN + "/" + case]
        process = subprocess.run(args, env=ENV, capture_output=True, text=True, timeout=12)
        assert process.returncode == 0 and "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["load_complete"] and meta["result"] == "complete", meta
        match = DONE.fullmatch(meta["status"])
        assert match and float(match[1]) >= DELAY - .1, meta
        assert meta["cursor_shape"] == 0, meta  # Qt::ArrowCursor, without a mouse event.
    print("Qt loading: full navigation duration and stationary cursor reset passed.")


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
ORIGIN = f"http://127.0.0.1:{server.server_port}"
try:
    with tempfile.TemporaryDirectory(prefix="wisp-loading-") as profile:
        (run_monkey if MODE == "--monkey" else run_qt)(profile)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
