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
value = "A\0B\rC\nD\r\nE*~ +é€😀\ud800\uf780\uf7ff"
encoded = {
    "UTF-8": "a%0D%0Aname=A%00B%0D%0AC%0D%0AD%0D%0AE*%7E+%2B%C3%A9%E2%82%AC%F0%9F%98%80"
             "%EF%BF%BD%EF%9E%80%EF%9F%BF&empty=",
    "windows-1252": "a%0D%0Aname=A%00B%0D%0AC%0D%0AD%0D%0AE*%7E+%2B%E9%80%26%23128512%3B"
                    "%26%2365533%3B%26%2363360%3B%26%2363487%3B&empty=",
    "x-mac-cyrillic": "a%0D%0Aname=A%00B%0D%0AC%0D%0AD%0D%0AE*%7E+%2B%26%23233%3B%FF%26%23128512%3B"
                      "%26%2365533%3B%26%2363360%3B%26%2363487%3B&empty=",
    "x-user-defined": "a%0D%0Aname=A%00B%0D%0AC%0D%0AD%0D%0AE*%7E+%2B%26%23233%3B%26%238364%3B"
                      "%26%23128512%3B%26%2365533%3B%80%FF&empty=",
}
normalized = {
    "UTF-8": "A\0B\r\nC\r\nD\r\nE*~ +é€😀\ufffd\uf780\uf7ff".encode(),
    "windows-1252": b"A\0B\r\nC\r\nD\r\nE*~ +\xe9\x80&#128512;&#65533;&#63360;&#63487;",
    "x-mac-cyrillic": b"A\0B\r\nC\r\nD\r\nE*~ +&#233;\xff&#128512;&#65533;&#63360;&#63487;",
    "x-user-defined": b"A\0B\r\nC\r\nD\r\nE*~ +&#233;&#8364;&#128512;&#65533;\x80\xff",
}
# accept-charset, document encoding, expected output encoding. Script assigns
# the attribute so its NUL reaches the DOM intact instead of HTML replacement.
charsets = {
    "absent": (None, "UTF-8", "UTF-8"),
    "empty": ("", "UTF-8", "UTF-8"),
    "whitespace": (" \t\n\f\r", "UTF-8", "UTF-8"),
    "legacy-first": ("not-utf-8 windows-1252 UTF-8", "UTF-8", "windows-1252"),
    "nul-label": ("utf-8\0suffix latin1", "UTF-8", "windows-1252"),
    "utf16": ("UTF-16 windows-1252", "UTF-8", "UTF-8"),
    "inherited": (None, "windows-1252", "windows-1252"),
    "empty-override": ("", "windows-1252", "UTF-8"),
    "mac-cyrillic": ("x-mac-ukrainian", "UTF-8", "x-mac-cyrillic"),
    "user-defined": ("x-user-defined", "UTF-8", "x-user-defined"),
}


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, body, encoding="UTF-8"):
        data = body.encode(encoding)
        self.send_response(200)
        self.send_header("Content-Type", f"text/html; charset={encoding}")
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
        if mode not in ("/get", "/post", "/multipart") or charset not in charsets:
            self.send_error(404)
            return
        method = "get" if mode == "/get" else "post"
        enctype = "multipart/form-data" if mode == "/multipart" else "application/x-www-form-urlencoded"
        accept, document_encoding, _expected = charsets[charset]
        charset_script = "" if accept is None else (
            f'document.getElementById("form").setAttribute("accept-charset", {json.dumps(accept)});')
        self.respond(f'''<!doctype html>
            <form id="form" action="/submit" method="{method}" enctype="{enctype}">
              <input id="field" type="hidden" name="original">
              <input name="empty" type="file"><input type="hidden" name="_charset_" value="wrong">
              <button id="send">Send</button>
            </form><script>
              document.getElementById("form").addEventListener("submit", event => {{
                if (!(event instanceof SubmitEvent) || !event.isTrusted ||
                    event.submitter !== document.getElementById("send")) throw Error("submit event");
                {charset_script}
                var field = document.getElementById("field");
                field.setAttribute("name", "a\\nname");
                field.setAttribute("value", {json.dumps(value)});
              }});
            </script>''', document_encoding)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f"http://127.0.0.1:{server.server_port}"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96", NO_PROXY="127.0.0.1")
try:
    with tempfile.TemporaryDirectory(prefix="wisp-form-data-") as tmp:
        for mode, charset in itertools.product(("get", "post", "multipart"), charsets):
            received.clear()
            expected_encoding = charsets[charset][2]
            expected_body = encoded[expected_encoding] + "&_charset_=" + expected_encoding
            dest = Path(tmp) / f"{mode}-{charset}"
            args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", "100", "--capture-timeout", "8000", "--enable_javascript=1",
                    "--capture-click-id", "send", "--capture-id", "destination", f"{origin}/{mode}?{charset}"]
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=12)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (mode, charset, result.stderr)
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (mode, charset, meta)
            assert len(received) == 1, (mode, received)
            method, path, content_type, body = received[0]
            if mode == "get":
                assert method == "GET" and path == "/submit?" + expected_body, received
            elif mode == "post":
                assert method == "POST" and path == "/submit", received
                assert body == expected_body.encode(), body
                assert content_type == "application/x-www-form-urlencoded", content_type
            else:
                message = BytesParser(policy=default).parsebytes(
                    b"Content-Type: " + content_type.encode() + b"\r\nMIME-Version: 1.0\r\n\r\n" + body)
                parts = list(message.iter_parts())
                assert len(parts) == 3, body
                assert parts[0].get_param("name", header="content-disposition") == "a%0D%0Aname", body
                assert parts[0].get_payload(decode=True) == normalized[expected_encoding], body
                assert parts[0]["Content-Type"] is None, body
                assert parts[1].get_param("name", header="content-disposition") == "empty", body
                assert parts[1].get_filename() == "", body
                assert parts[1].get_content_type() == "application/octet-stream", body
                assert parts[1].get_payload(decode=True) == b"", body
                assert parts[2].get_param("name", header="content-disposition") == "_charset_", body
                assert parts[2].get_payload(decode=True) == expected_encoding.encode(), body
finally:
    server.shutdown()
    server.server_close()
    thread.join()
print("30 GET/POST cases preserve values and select the specified encoding, including _charset_")
