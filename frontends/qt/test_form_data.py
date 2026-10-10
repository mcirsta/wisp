#!/usr/bin/env python3
"""Check actual GET/POST bytes after script edits and a native submit click."""
from email.parser import BytesParser
from email.policy import default
import http.server
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
received = []
value = "A\0B\rC\nD\r\nE*~ +é😀\ud800"
encoded = "a%0D%0Aname=A%00B%0D%0AC%0D%0AD%0D%0AE*%7E+%2B%C3%A9%F0%9F%98%80%EF%BF%BD&empty="
normalized = "A\0B\r\nC\r\nD\r\nE*~ +é😀\ufffd".encode()
charset_attributes = {"absent": "", "empty": ' accept-charset=""',
                      "whitespace": ' accept-charset=" \t\n\f\r"'}


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, body):
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        received.append((self.command, self.path, self.headers["Content-Type"], body))
        self.respond('<!doctype html><p id="destination">Submitted</p>')

    def do_GET(self):
        if self.path.startswith("/submit"):
            received.append((self.command, self.path, "", b""))
            self.respond('<!doctype html><p id="destination">Submitted</p>')
            return
        mode, _, charset = self.path.partition("?")
        if mode not in ("/get", "/post", "/multipart") or charset not in charset_attributes:
            self.send_error(404)
            return
        method = "get" if mode == "/get" else "post"
        enctype = "multipart/form-data" if mode == "/multipart" else "application/x-www-form-urlencoded"
        self.respond(f'''<!doctype html><meta charset="utf-8">
            <form action="/submit" method="{method}" enctype="{enctype}"{charset_attributes[charset]}>
              <input id="field" type="hidden" name="original">
              <input name="empty" type="file"><button id="send">Send</button>
            </form><script>
              var field = document.getElementById("field");
              field.setAttribute("name", "a\\nname");
              field.setAttribute("value", {json.dumps(value)});
            </script>''')


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f"http://127.0.0.1:{server.server_port}"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96", NO_PROXY="127.0.0.1")
try:
    with tempfile.TemporaryDirectory(prefix="wisp-form-data-") as tmp:
        for mode, charset in itertools.product(("get", "post", "multipart"), charset_attributes):
            received.clear()
            dest = Path(tmp) / f"{mode}-{charset}"
            args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=1",
                    "--capture-click-id", "send", "--capture-id", "destination", f"{origin}/{mode}?{charset}"]
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=12)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (mode, result.stderr)
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (mode, meta)
            assert len(received) == 1, (mode, received)
            method, path, content_type, body = received[0]
            if mode == "get":
                assert method == "GET" and path == "/submit?" + encoded, received
            elif mode == "post":
                assert method == "POST" and path == "/submit", received
                assert body == encoded.encode(), body
                assert content_type == "application/x-www-form-urlencoded", content_type
            else:
                message = BytesParser(policy=default).parsebytes(
                    b"Content-Type: " + content_type.encode() + b"\r\nMIME-Version: 1.0\r\n\r\n" + body)
                parts = list(message.iter_parts())
                assert len(parts) == 2, body
                assert parts[0].get_param("name", header="content-disposition") == "a%0D%0Aname", body
                assert parts[0].get_payload(decode=True) == normalized, body
                assert parts[0]["Content-Type"] is None, body
                assert parts[1].get_param("name", header="content-disposition") == "empty", body
                assert parts[1].get_filename() == "", body
                assert parts[1].get_content_type() == "application/octet-stream", body
                assert parts[1].get_payload(decode=True) == b"", body
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print("GET, URL-encoded POST and multipart preserve Unicode, NULs, newlines and empty files")
