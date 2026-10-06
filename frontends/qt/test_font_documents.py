#!/usr/bin/env python3
"""Document-scoped CSS font bindings over real HTTP, navigation and tabs."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary = Path(sys.argv[1]).resolve()
fonts = Path(__file__).resolve().parents[2] / "test/fonts"
requests = []
slow_started = threading.Event()
slow_finished = threading.Event()
release_slow = threading.Event()


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        kind = "text/html; charset=utf-8"
        if self.path == "/styles/shared.css":
            kind = "text/css"
            body = b'@import url(imported.css);'
        elif self.path == "/styles/imported.css":
            kind = "text/css"
            body = b'@font-face{font-family:Shared;src:url(../a.ttf)}'
        elif self.path in ("/a.ttf", "/b.ttf", "/slow.ttf"):
            kind = "font/ttf"
            if self.path == "/slow.ttf":
                slow_started.set()
                release_slow.wait(10)
            body = (fonts / ("isolation-a.ttf" if self.path == "/a.ttf" else "isolation-b.ttf")).read_bytes()
        elif self.path == "/favicon.ico":
            self.send_error(404)
            return
        else:
            page = self.path[1:]
            body = f'<!doctype html><title>{page}</title>'
            if page in ("a", "shared"):
                body += '<link rel="stylesheet" href="/styles/shared.css">'
            elif page in ("b", "slow"):
                body += f'<style>@font-face{{font-family:Shared;src:url(/{page}.ttf)}}</style>'
            body += ('<style>.sample{display:inline-block;font:20px/30px Shared,sans-serif}'
                     '#control{font-family:sans-serif}</style>'
                     '<span class="sample" id="sample">MMMM</span>'
                     '<span class="sample" id="control">MMMM</span>')
            for destination in ("a", "b", "none", "shared"):
                body += f'<p><a id="{destination}" href="/{destination}">{destination}</a></p>'
            body += '<p><a id="tab" href="/b" target="_blank">Background tab</a></p>'
            body += '<p><a id="slowtab" href="/slow" target="_blank">Pending background tab</a></p>'
            body = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "public, max-age=3600")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass
        if self.path == "/slow.ttf":
            slow_finished.set()


def widths(info):
    found = {}
    for element in info["elements"]:
        assert element["status"] == "ok" and element.get("boxes"), element
        found[element["id"]] = element["boxes"][0]["content"]["width"]
    return found


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
origin = f"http://127.0.0.1:{server.server_port}"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96", NO_PROXY="127.0.0.1")
try:
    with tempfile.TemporaryDirectory(prefix="wisp-font-documents-") as tmp:
        for mode, actions in (("navigate", ("b", "none", "shared", "tab")),
                              ("pending", ("slowtab", "none"))):
            requests.clear()
            dest = Path(tmp) / mode
            args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", "300", "--capture-timeout", "8000",
                    "--capture-id", "sample", "--capture-id", "control"]
            for action in actions:
                args += ["--capture-click-id", action]
            result = subprocess.run([*args, origin + "/a"], env=env, capture_output=True,
                                    text=True, timeout=15)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (mode, result.stderr)
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (mode, meta, requests)
            steps = json.loads((dest / "steps.json").read_text())
            states = [widths(step) for step in steps]
            final = widths(json.loads((dest / "inspection.json").read_text()))
            assert states[0]["sample"] == 40, states
            if mode == "navigate":
                assert states[1]["sample"] == 72, states  # Same CSS/native names, different resource.
                assert states[2]["sample"] == states[2]["control"], states  # No face declared here.
                assert states[3]["sample"] == final["sample"] == 40, states
                assert requests.count("/a.ttf") == requests.count("/b.ttf") == 1, requests
                assert requests.count("/styles/shared.css") == 1, requests  # Shared cached CSS still registers faces.
                assert len(meta["icons"]["tabs"]) == 2, meta
            else:
                assert slow_started.is_set() and not slow_finished.is_set(), requests
                assert final["sample"] == final["control"], final
                # A different tab's pending font must neither stall this page
                # nor keep a download callback alive after browser shutdown.
finally:
    release_slow.set()
    server.shutdown()
    server.server_close()
    thread.join()

print("Font document isolation: navigation, cached stylesheets/bytes, simultaneous tabs and pending shutdown passed.")
