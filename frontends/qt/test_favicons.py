#!/usr/bin/env python3
"""Window branding stays fixed; favicons belong to their document's tab."""
import hashlib
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


def png(colour):
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))

    scanlines = (b"\0" + bytes(colour) * 16) * 16
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", 16, 16, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


colours = {"red": (255, 0, 0, 255), "blue": (0, 0, 255, 255), "green": (0, 255, 0, 255)}
digests = {name: hashlib.sha256(bytes(colour) * 256).hexdigest() for name, colour in colours.items()}
requests = []
slow_started = threading.Event()
background_opened = threading.Event()


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        requests.append(self.path)
        if self.path == "/green":
            background_opened.set()
        kind = "text/html; charset=utf-8"
        if self.path in ("/favicon.ico", "/missing.png"):
            self.send_error(404)
            return
        if self.path.endswith(".png"):
            kind = "image/png"
            colour = self.path[1:-4]
            if colour == "slow":
                slow_started.set()
                time.sleep(1.5)
                colour = "red"
            elif colour == "tab-slow":
                slow_started.set()
                # Keep the first tab's favicon pending until the second tab
                # opens, independent of machine speed during initial loading.
                background_opened.wait(timeout=8)
                colour = "red"
            elif colour == "wait":
                # Let the previous document's delayed favicon finish before
                # inspecting the replacement page (it must remain blue).
                time.sleep(2)
                colour = "blue"
            body = png(colours[colour])
        else:
            page = self.path[1:]
            icon = {"red": "red", "slow": "slow", "tabs": "tab-slow", "green": "green",
                    "blue": "blue", "none": None, "broken": "missing"}[page]
            body = f'<!doctype html><meta charset="utf-8"><title>{page}</title>'
            if icon:
                body += f'<link rel="icon" href="/{icon}.png">'
            body += '<p><a id="next" href="/blue">Next page</a></p>'
            body += '<p><a id="none" href="/none">No icon</a></p>'
            body += '<p><a id="broken" href="/broken">Broken icon</a></p>'
            body += '<p><a id="tab" href="/green" target="_blank">Another tab</a></p>'
            if page == "blue":
                body += '<img src="/wait.png" alt="">'
            body = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Cancelling an old document's favicon is expected.


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
binary = Path(sys.argv[1]).resolve()
origin = f"http://127.0.0.1:{server.server_port}"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96", NO_PROXY="127.0.0.1")
try:
    with tempfile.TemporaryDirectory(prefix="wisp-favicons-") as tmp:
        for number, (page, actions, settle) in enumerate((("red", ("next", "none", "broken"), 150),
                ("slow", ("next",), 150), ("tabs", ("tab",), 1000), ("slow", (), 150))):
            requests.clear()
            slow_started.clear()
            background_opened.clear()
            dest = Path(tmp) / f"{number}-{page}"
            args = [str(binary), "--capture-dir", str(dest), "--capture-size", "640x480",
                    "--capture-settle", str(settle), "--capture-timeout", "10000"]
            for action in actions:
                args += ["--capture-click-id", action]
            result = subprocess.run([*args, origin + "/" + page], env=env, capture_output=True,
                                    text=True, timeout=15)
            assert result.returncode == 0 and "runtime error:" not in result.stderr, (page, result.stderr)
            meta = json.loads((dest / "capture.json").read_text())
            assert meta["result"] == "complete" and meta["load_complete"], (page, meta)
            steps = json.loads((dest / "steps.json").read_text()) if actions else []
            icons = [step["icons"] for step in steps]
            brand = meta["icons"]["application"]
            assert brand and brand not in digests.values(), meta["icons"]
            for state in [*icons, meta["icons"]]:
                assert state["application"] == state["window"] == brand, (page, state)
            if page == "red":
                assert icons[0]["tabs"][0]["icon"] == digests["red"], icons
                assert icons[1]["tabs"][0]["icon"] == digests["blue"], icons
                default = icons[2]["tabs"][0]["icon"]
                assert default and default not in digests.values(), icons
                assert icons[3]["tabs"][0]["icon"] == default, icons
                assert "/missing.png" in requests and "/favicon.ico" in requests, requests
            elif page == "slow":
                assert slow_started.is_set(), requests
                if actions:
                    assert icons[0]["tabs"][0]["icon"] != digests["red"], icons
                    assert icons[1]["tabs"][0]["icon"] == digests["blue"], (icons, requests)
                else:
                    assert meta["icons"]["tabs"][0]["icon"] != digests["red"], meta["icons"]
            else:
                assert slow_started.is_set(), requests
                assert icons[0]["tabs"][0]["icon"] != digests["red"], icons
                tabs = {tab["title"]: tab for tab in icons[1]["tabs"]}
                assert set(tabs) == {"tabs", "green"}, icons
                assert tabs["tabs"]["icon"] == digests["red"], icons
                assert tabs["green"]["icon"] == digests["green"], icons
                # target=_blank opens a background tab with the default settings.
                assert tabs["tabs"]["current"] and not tabs["green"]["current"], icons
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print("Fixed window branding; navigation, missing icons, late responses and background tab favicons passed.")
