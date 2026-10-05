#!/usr/bin/env python3
"""HTML object fallback on failed HTTP responses, including shared cache hits.

Expectations follow the object representation algorithm's failed-load step:
https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
"""
import base64
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from threading import Event, Thread
import time

binary = Path(sys.argv[1]).resolve()
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
gif = base64.b64decode("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")
requests = Counter()
pending_kinds = ("image", "document", "failure")
probes = {key: Event() for key in pending_kinds}
missed_probes = []
failures = ("missing", "server", "html", "empty", "redirect-bad", "image-first", "object-first")
successes = ("ok", "partial", "redirect-ok")
sources = {"missing": "/missing.gif", "server": "/server.gif", "html": "/error.html",
           "empty": "/empty", "redirect-bad": "/redirect-bad", "ok": "/ok.gif",
           "partial": "/partial.gif", "redirect-ok": "/redirect-ok",
           "image-first": "/image-first.gif", "object-first": "/object-first.gif"}


def object_element(key):
    return f'<object data="{sources[key]}"><span id="{key}">{key} fallback</span></object>'


page = ('<!doctype html><style>body{font:14px/20px sans-serif}object{display:block}</style>'
        '<img id="warm-image" src="/warm.gif">'
        '<img id="shared-image-first" src="/image-first.gif">' + object_element("image-first") +
        object_element("object-first") + '<img id="shared-object-first" src="/object-first.gif">' +
        ''.join(object_element(key) for key in (*failures[:-2], *successes)) +
        '<details id="late"><summary id="summary">Reveal cached object</summary>'
        '<object data="/warm.gif"><span id="warm">Cached failure fallback</span></object></details>')
error_page = b'<!doctype html><p id="error-body">Readable HTTP error page</p>'
pending_page = ('<!doctype html><style>object{display:block}object::before{content:"provisional token"}'
                '#pending-document{width:240px;height:60px}</style>'
                '<details open><summary id="pending-summary">Objects</summary>' + ''.join(
                    f'<object id="pending-{key}" data="/pending-{key}">'
                    f'<span id="fallback-{key}">Provisional {key} fallback</span>'
                    f'<object data="/probe-{key}"></object></object>' for key in pending_kinds) + '</details>')

# Find-in-page makes closed details / hidden-until-found contents searchable
# synchronously. A resource candidate is still fallback until it is available:
# https://html.spec.whatwg.org/multipage/interaction.html#interaction-with-details-and-hidden=until-found
search_cases = {f'{wrapper}-{result}-{source}': (wrapper, result, source)
                for wrapper in ('details', 'hidden') for result in ('failure', 'success')
                for source in ('body', 'before')}


def search_page(key):
    wrapper, _, _ = search_cases[key]
    opening = '<details id="panel"><summary id="summary">Reveal</summary>' if wrapper == 'details' else (
        '<div id="panel" hidden="until-found">')
    closing = '</details>' if wrapper == 'details' else '</div>'
    return ('<!doctype html><style>object,#fallback{display:block}'
            'object::before{content:attr(data-before)}</style>'
            '<p id="outside">searchable outside</p>' + opening +
            f'<object id="owner" data="/search-resource/{key}" data-before="searchable {key}-before">'
            f'<span id="fallback">searchable {key}-body</span></object>' + closing +
            '<div hidden><object data="/forbidden"><span>searchable forbidden</span></object></div>'
            '<div style="content-visibility:hidden"><object data="/forbidden">searchable forbidden</object></div>')


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        requests[self.path] += 1
        status, mime, body = 200, "image/gif", gif
        headers = {}
        if self.path == "/":
            mime, body = "text/html", page.encode()
        elif self.path == "/pending":
            mime, body = "text/html", pending_page.encode()
        elif self.path.startswith("/search/"):
            mime, body = "text/html", search_page(self.path.removeprefix("/search/")).encode()
        elif self.path.startswith("/search-resource/"):
            key = self.path.removeprefix("/search-resource/")
            # Let the reveal layout finish before the representation changes.
            time.sleep(0.2)
            status = 404 if search_cases[key][1] == 'failure' else 200
        elif self.path.startswith("/pending-"):
            key = self.path.removeprefix("/pending-")
            # A nested object requested by the fallback releases the parent.
            # Images would not prove this: the parser preloads them separately.
            # This proves construction happened before the resource became
            # available, independently of scheduler speed or screenshot timing.
            if not probes[key].wait(5):
                missed_probes.append(key)
            if key == "document":
                mime, body = "text/html", b'<!doctype html><p>Embedded document</p>'
            elif key == "failure":
                status = 404
        elif self.path.startswith("/probe-"):
            probes[self.path.removeprefix("/probe-")].set()
        elif self.path == "/error.html":
            status, mime, body = 404, "text/html", error_page
        elif self.path == "/empty":
            status, body = 404, b""
        elif self.path in ("/redirect-bad", "/redirect-ok"):
            status, body = 302, b""
            headers["Location"] = "/redirect-target.gif" if self.path == "/redirect-bad" else "/ok.gif"
        elif self.path == "/partial.gif":
            status = 206
            headers["Content-Range"] = f"bytes 0-{len(gif) - 1}/{len(gif)}"
        elif self.path == "/server.gif":
            status = 500
        elif self.path != "/ok.gif":
            status = 404
        if self.path in ("/image-first.gif", "/object-first.gif"):
            # Both consumers attach while the same underlying fetch is pending.
            time.sleep(0.1)
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Rejecting the object's response may cancel its body download.

    def log_message(self, *args):
        pass


def capture(out, url, ids, actions):
    args = [str(binary), "--capture-dir", str(out), "--capture-size", "700x600",
            "--capture-settle", "50", "--capture-timeout", "15000", "--enable_javascript=0"]
    for key in ids:
        args += ["--capture-id", key]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    result = subprocess.run([*args, url], env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0 and "runtime error:" not in result.stderr, result.stderr
    meta = json.loads((out / "capture.json").read_text())
    assert meta["result"] == "complete" and meta["load_complete"], meta
    steps = json.loads((out / "steps.json").read_text())
    assert len(steps) == len(actions) + 1, len(steps)
    return steps


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    origin = f"http://127.0.0.1:{server.server_port}"
    with tempfile.TemporaryDirectory(prefix="wisp-object-http-") as tmp:
        root = Path(tmp)
        ids = (*failures, *successes, "warm", "late", "warm-image", "shared-image-first", "shared-object-first")
        actions = [("click-id", "summary"), ("click-id", "summary"),
                   ("find", "Cached failure fallback"), ("resize", "600x500")]
        steps = capture(root / "objects", origin + "/", ids, actions)
        for index, step in enumerate(steps):
            elements = {e["id"]: e for e in step["elements"]}
            for key in failures:
                assert elements[key]["boxes"], (index, key, elements[key])
            for key in successes:
                assert not elements[key]["boxes"], (index, key, elements[key])
            assert bool(elements["warm"]["boxes"]) == (index in (1, 3, 4)), (index, elements["warm"])
            for key in ("warm-image", "shared-image-first", "shared-object-first"):
                rect = elements[key]["boxes"][0]["content"]
                assert (rect["width"], rect["height"]) == (1, 1), (index, key, rect)
            assert step["active_resources"] == 0, (index, step["active_resources"])
            if index > 1:
                assert step["resource_count"] == steps[1]["resource_count"], (index, step["resource_count"])
        for path in ("/warm.gif", "/image-first.gif", "/object-first.gif", "/missing.gif", "/server.gif"):
            assert requests[path] == 1, (path, requests)

        # The same response is a readable document when visited directly.
        steps = capture(root / "navigation", origin + "/error.html", ("error-body",),
                        [("find", "Readable HTTP error page")])
        assert steps[0]["elements"][0]["boxes"], steps[0]
        assert steps[1]["elements"][0]["boxes"][0]["highlighted_fragments"] == 1, steps[1]

        ids = tuple(f"{prefix}-{key}" for key in pending_kinds for prefix in ("pending", "fallback"))
        steps = capture(root / "pending", origin + "/pending", ids,
                        [("click-id", "pending-summary"), ("click-id", "pending-summary"),
                         ("resize", "600x500")])
        assert not missed_probes, ("Fallback was not constructed while loading", missed_probes)
        for index, step in enumerate(steps):
            elements = {e["id"]: e for e in step["elements"]}
            for key in pending_kinds:
                assert bool(elements[f"fallback-{key}"]["boxes"]) == (key == "failure" and index != 1), (
                    index, key, elements[f"fallback-{key}"])
            if index != 1:
                rect = elements["pending-image"]["boxes"][0]["content"]
                assert (rect["width"], rect["height"]) == (1, 1), (index, rect)
                for key in ("image", "document"):
                    box = elements[f"pending-{key}"]["boxes"][0]
                    assert box["child_count"] == 0, (index, key, box["children"])
            assert step["active_resources"] == 0, (index, step["active_resources"])
            assert step["resource_count"] == steps[0]["resource_count"] == 6, (index, step["resource_count"])
        for key in pending_kinds:
            assert requests[f"/pending-{key}"] == requests[f"/probe-{key}"] == 1, (key, requests)

        for key, (wrapper, result, source) in search_cases.items():
            query = f'{key}-{source}'
            actions = [('find-all', 'searchable'), ('find', query), ('find', query)]
            if wrapper == 'details':
                actions += [('click-id', 'summary'), ('find', query)]
            steps = capture(root / key, origin + '/search/' + key,
                            ('panel', 'owner', 'fallback', 'outside'), actions)
            for index, step in enumerate(steps):
                elements = {e['id']: e for e in step['elements']}
                assert step['active_resources'] == 0, (key, index, step['active_resources'])
                # Merely collecting hidden matches must neither load resources
                # nor open ancestors. Only choosing a match reveals its owner.
                assert step['resource_count'] == (0 if index < 2 else 1), (key, index, step['resource_count'])
                if index == 1:
                    assert elements['outside']['boxes'][0]['highlighted_fragments'] == 1, (key, index)
                revealed = index >= 2 and not (wrapper == 'details' and
                                               (index == 4 or (index == 5 and result == 'success')))
                panel = elements['panel']['state']
                assert (panel['open'] if wrapper == 'details' else panel['hidden'] is None) == revealed, (
                    key, index, panel)
                assert bool(elements['owner']['boxes']) == revealed, (key, index, elements['owner'])
                assert bool(elements['fallback']['boxes']) == (revealed and result == 'failure'), (
                    key, index, elements['fallback'])
                if revealed and result == 'success':
                    assert elements['owner']['boxes'][0]['child_count'] == 0, (key, index, elements['owner'])
                if revealed and result == 'failure':
                    match = elements['owner' if source == 'before' else 'fallback']['boxes'][0]
                    assert match['highlighted_fragments'] == 1, (key, index, match['highlighted_fragments'])
            assert requests['/search-resource/' + key] == 1, (key, requests)
        assert requests['/forbidden'] == 0, requests
finally:
    server.shutdown()
    thread.join()
    server.server_close()
print("HTTP object fallback: errors, cache reuse, pending replacement and search/reveal before construction passed.")
