#!/usr/bin/env python3
"""Optional integration checks; requires Chromium, ImageMagick 7, websocket-client."""

import http.server
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

from compare_capture import png_size


binary = Path(sys.argv[1]).resolve()
script = Path(__file__).with_name("compare_capture.py").resolve()
fixture = script.parents[2] / "test/render-inspect.html"


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/late.css":
            time.sleep(0.8)
            body, mime = b"#probe { width: 81px; }", "text/css"
        else:
            body = fixture.read_bytes() + b'<link rel="stylesheet" href="/late.css">'
            mime = "text/html"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


with tempfile.TemporaryDirectory(prefix="wisp-comparison-test-") as tmp:
    root = Path(tmp)

    def run(name, url=fixture, *extra, code=0):
        destination = root / name
        process = subprocess.run([sys.executable, str(script), str(url), "--wisp", str(binary),
            "--output", str(destination), "--size", "320x240", "--timeout", "8", "--settle-ms", "100",
            "--id", "probe", "--id", "hidden", "--id", "absent", "--id", "unicode:ș",
            "--point", "60,80", *extra], capture_output=True, text=True, timeout=35)
        assert process.returncode == code, (process.stdout, process.stderr)
        metadata = json.loads((destination / "comparison.json").read_text())
        assert metadata["result"] == ("complete" if code == 0 else "error"), metadata
        return destination, metadata

    first, meta = run("first")
    assert not meta["reference"]["javascript"]
    assert meta["reference"]["viewport"] == {"width": 320, "height": 240}
    for name in ("overlay.png", "difference.png", "wisp/page.png", "reference/page.png"):
        assert png_size(first / name) == (320, 240)
    reference = json.loads((first / "reference/inspection.json").read_text())
    elements = {element["id"]: element for element in reference["elements"]}
    # The fixture's script would change this to 999px if page JS ran.
    assert elements["probe"]["computed_style"]["width"] == "80px", elements["probe"]
    assert elements["hidden"]["status"] == "no_box"
    assert elements["absent"]["status"] == "not_found"
    assert elements["unicode:ș"]["status"] == "ok"
    assert reference["point"]["status"] == "ok"
    assert 0 <= meta["different_pixel_fraction"] <= 1
    assert (first / "report.html").is_file()

    _, repeated = run("repeated", fixture, "--reference", str(first / "wisp"))
    assert repeated["different_pixel_fraction"] == 0, repeated
    # A mismatched reference must fail rather than resize either image.
    ref_path = first / "wisp/capture.json"
    changed = json.loads(ref_path.read_text())
    changed["viewport"]["width"] += 1
    ref_path.write_text(json.dumps(changed))
    run("mismatch", fixture, "--reference", str(first / "wisp"), code=1)
    run("missing", (root / "missing.html").as_uri(), code=1)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        delayed, _ = run("delayed", f"http://127.0.0.1:{server.server_port}/page")
        ref = json.loads((delayed / "reference/inspection.json").read_text())
        assert ref["elements"][0]["computed_style"]["width"] == "81px", ref
        actual = json.loads((delayed / "wisp/inspection.json").read_text())
        assert actual["elements"][0]["boxes"][0]["content"]["width"] == 81, actual
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

print("Comparison checks passed (Chromium/JS off, inspection, exact viewport, delayed CSS, baseline, failures).")
