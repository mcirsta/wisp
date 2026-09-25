#!/usr/bin/env python3
"""Integration check for WISP_QT_CAPTURE; Python stdlib + Qt offscreen plugin only."""

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


binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/render-capture.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/page")
            self.end_headers()
            return
        if self.path == "/slow.css":
            time.sleep(0.8)
            body = b"#probe { width: 321px; }"
            mime = "text/css"
        elif self.path == "/pending":
            time.sleep(2)
            body, mime = fixture.read_bytes(), "text/html"
        elif self.path == "/write-failure":
            # The directory was empty at launch; cause an output failure while loading.
            (root / "write-failure" / "page.png").mkdir()
            body, mime = fixture.read_bytes(), "text/html"
        else:
            body = fixture.read_bytes() + b'<link rel="stylesheet" href="/slow.css">'
            mime = "text/html"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # The timeout test intentionally aborts an outstanding fetch.


with tempfile.TemporaryDirectory(prefix="wisp-capture-test-") as tmp:
    root = Path(tmp)
    # A real file with spaces also exercises the existing URL conversion path.
    local = root / "fixture with spaces.html"
    local.write_bytes(fixture.read_bytes())

    def run(name, url, *options, code=0, result="complete", size=(640, 480)):
        destination = root / name
        args = [str(binary), "--capture-dir", str(destination), "--capture-size",
                f"{size[0]}x{size[1]}", "--capture-settle", "100",
                "--capture-timeout", "8000", *options, str(url)]
        process = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15)
        assert process.returncode == code, (args, process.returncode, process.stderr)
        meta = json.loads((destination / "capture.json").read_text())
        assert meta["result"] == result, meta
        assert meta["viewport"] == {"width": size[0], "height": size[1]}, meta
        assert meta["javascript"] is False and meta["animate_images"] is False, meta
        assert meta["dpi"] == 96 and meta["zoom"] == 1, meta
        if meta["artifacts"]["page.png"]:
            png = (destination / "page.png").read_bytes()
            assert png[:8] == b"\x89PNG\r\n\x1a\n"
            assert struct.unpack(">II", png[16:24]) == size
        if code == 0:
            assert meta["load_complete"] and all(meta["artifacts"].values()), meta
            assert "capture-title" in (destination / "dom.txt").read_text()
            assert "123px" in (destination / "boxes.txt").read_text()
        return destination, meta

    first, _ = run("first", local)
    second, _ = run("repeat", local)
    assert (first / "page.png").read_bytes() == (second / "page.png").read_bytes()
    run("narrow", local, size=(320, 240))
    run("timeout", local, "--capture-settle", "2000", "--capture-timeout", "200",
        code=1, result="timeout")
    run("missing", (root / "missing.html").as_uri(), code=1, result="load_error")
    inspected, _ = run("inspected", fixture.with_name("render-inspect.html"),
        "--capture-id", "probe", "--capture-id", "absolute", "--capture-id", "hidden",
        "--capture-id", "absent", "--capture-id", "unicode:ș", "--capture-id", "flex",
        "--capture-id", "wrapped", "--capture-point", "60,80")
    inspection = json.loads((inspected / "inspection.json").read_text())
    elements = {item["id"]: item for item in inspection["elements"]}
    probe = elements["probe"]["boxes"][0]
    assert probe["node"]["path"] == "/html[1]/body[1]/div[1]/div[1]", probe
    assert probe["content"] == {"x": 47, "y": 47, "width": 80, "height": 40}, probe
    assert probe["border_box"] == {"x": 39, "y": 39, "width": 96, "height": 56}, probe
    assert probe["padding_trbl"] == [5, 5, 5, 5]
    # Used right margin absorbs the unused width of this fixed-width block.
    assert probe["margin_trbl"] == [7, 137, 7, 7], probe["margin_trbl"]
    assert probe["font"]["pixel_size"] == 16 and probe["font"]["resolved_family"]
    assert "width: 80px" in probe["computed_style_dump"]
    assert elements["absolute"]["boxes"][0]["absolute_containing_block"]["node"]["id"] == "container"
    assert elements["hidden"]["status"] == "no_box", elements["hidden"]
    assert elements["absent"]["status"] == "not_found", elements["absent"]
    assert elements["unicode:ș"]["status"] == "ok"
    assert elements["flex"]["boxes"][0]["layout_style"]["flex-direction"] == "column"
    assert elements["wrapped"]["box_count"] > 0
    assert any(hit["node"]["id"] == "probe" for hit in inspection["point"]["hits"])
    # Refuse stale captures and invalid sizes, before starting the GUI.
    for options in (["--capture-dir", str(first)],
                    ["--capture-dir", str(root / "invalid"), "--capture-size", "0x480"],
                    ["--capture-dir", str(root / "invalid"), "--capture-point", "x,2"],
                    ["--capture-dir", str(root / "invalid"), str(local)]):
        process = subprocess.run([str(binary), *options, str(local)], env=env,
                                 capture_output=True, timeout=5)
        assert process.returncode == 2, process.stderr

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        origin = f"http://127.0.0.1:{server.server_port}"
        delayed, meta = run("delayed", origin + "/redirect")
        assert meta["elapsed_ms"] >= 800, meta
        assert meta["content_url"] == origin + "/page", meta
        probe = next(line for line in (delayed / "boxes.txt").read_text().splitlines()
                     if "ID:probe " in line)
        assert " w321 " in probe, probe
        _, meta = run("pending", origin + "/pending", "--capture-timeout", "200",
                      code=1, result="timeout")
        assert not meta["load_complete"] and not meta["artifacts"]["page.png"], meta
        _, meta = run("write-failure", origin + "/write-failure", code=1, result="output_error")
        assert meta["load_complete"] and not meta["artifacts"]["page.png"], meta
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

print("Qt capture checks passed (viewport, repeatability, dumps, delayed CSS, redirect, timeout, errors, CLI).")
