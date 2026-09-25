#!/usr/bin/env python3
"""Optional developer tool: Wisp vs Chromium (or an earlier capture), with JS off.

Requires ImageMagick 7. Chromium mode also needs Python websocket-client.
No browser downloads, site scripts, or changes to the user's browser profile.
"""

import argparse
import base64
import html
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def png_size(path):
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"Not a PNG: {path}")
    return struct.unpack(">II", data[16:24])


class ProtocolError(RuntimeError):
    pass


class DevTools:
    def __init__(self, url, deadline):
        import websocket  # Only required for a new Chromium reference.
        self.ws = websocket.create_connection(url, timeout=5, suppress_origin=True,
                                               http_no_proxy=["127.0.0.1", "localhost"])
        self.deadline = deadline
        self.sequence = 0
        self.pending = set()
        self.request_urls = {}
        self.loaded = set()
        self.last_network = time.monotonic()
        self.failures = []

    def receive(self, wait=None):
        import websocket
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Chromium did not finish within the capture deadline")
        self.ws.settimeout(min(remaining, wait) if wait else remaining)
        try:
            message = json.loads(self.ws.recv())
        except websocket.WebSocketTimeoutException:
            if wait:
                return {}
            raise TimeoutError("Chromium DevTools response timed out")
        event, data = message.get("method"), message.get("params", {})
        if event == "Network.requestWillBeSent":
            self.pending.add(data["requestId"])
            self.request_urls[data["requestId"]] = data["request"]["url"]
            self.last_network = time.monotonic()
        elif event in ("Network.loadingFinished", "Network.loadingFailed"):
            self.pending.discard(data["requestId"])
            self.last_network = time.monotonic()
            if event == "Network.loadingFailed":
                self.failures.append({"url": self.request_urls.get(data["requestId"]), "error": data.get("errorText")})
        elif event == "Network.responseReceived" and data["response"]["status"] >= 400:
            self.failures.append({"url": data["response"]["url"], "http_status": data["response"]["status"]})
        elif event == "Page.lifecycleEvent" and data["name"] == "load":
            self.loaded.add(data["loaderId"])
        return message

    def call(self, method, **params):
        self.sequence += 1
        self.ws.send(json.dumps({"id": self.sequence, "method": method, "params": params}))
        while True:
            message = self.receive()
            if message.get("id") == self.sequence:
                if "error" in message:
                    raise ProtocolError(f"{method}: {message['error']['message']}")
                return message.get("result", {})

    def wait_loaded(self, loader, settle_ms):
        while (loader not in self.loaded or self.pending or
               (time.monotonic() - self.last_network) * 1000 < settle_ms):
            self.receive(0.05)


def inspect_chromium(cdp, ids, point):
    cdp.call("DOM.enable")
    cdp.call("CSS.enable")
    root = cdp.call("DOM.getDocument", depth=-1)["root"]
    nodes = {}
    pending = [root]
    while pending:
        node = pending.pop()
        attrs = dict(zip(node.get("attributes", [])[::2], node.get("attributes", [])[1::2]))
        if "id" in attrs:
            nodes.setdefault(attrs["id"], node)
        pending.extend(reversed(node.get("children", [])))

    def describe(node):
        node_id = node["nodeId"]
        style = cdp.call("CSS.getComputedStyleForNode", nodeId=node_id)["computedStyle"]
        wanted = ("display", "position", "box-sizing", "width", "height", "min-width", "max-width",
                  "min-height", "max-height", "overflow", "overflow-x", "overflow-y", "color",
                  "background-color", "font-family", "font-size", "font-weight", "line-height",
                  "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis", "order",
                  "align-items", "align-self", "justify-content", "gap", "row-gap", "column-gap",
                  "grid-template-columns", "grid-template-rows", "grid-auto-flow", "transform")
        result = {"tag": node["nodeName"].lower(), "computed_style":
                  {v["name"]: v["value"] for v in style if v["name"] in wanted}}
        try:
            result["box_model"] = cdp.call("DOM.getBoxModel", nodeId=node_id)["model"]
            result["status"] = "ok"
        except ProtocolError as error:
            result.update(status="no_box", detail=str(error))
        result["fonts"] = cdp.call("CSS.getPlatformFontsForNode", nodeId=node_id)["fonts"]
        return result

    elements = [{"id": key, **(describe(nodes[key]) if key in nodes else {"status": "not_found"})}
                for key in ids]
    result = {"elements": elements, "coordinate_space": "document CSS pixels; quads include transforms"}
    if point:
        x, y = map(int, point.split(","))
        try:
            hit = cdp.call("DOM.getNodeForLocation", x=x, y=y)
            node = cdp.call("DOM.describeNode", backendNodeId=hit["backendNodeId"])["node"]
            # Resolve the frontend node ID if describeNode only returned a backend ID.
            if not node.get("nodeId"):
                node["nodeId"] = cdp.call("DOM.pushNodesByBackendIdsToFrontend",
                    backendNodeIds=[hit["backendNodeId"]])["nodeIds"][0]
            result["point"] = {"x": x, "y": y, **describe(node)}
        except ProtocolError as error:
            result["point"] = {"x": x, "y": y, "status": "unavailable", "detail": str(error)}
    return result


def chromium_capture(args, url, directory, wisp_meta):
    directory.mkdir()
    deadline = time.monotonic() + args.timeout
    cdp = None
    with tempfile.TemporaryDirectory(prefix="wisp-reference-") as profile, \
            (directory / "browser.log").open("w") as log:
        # Profile-wide JS blocking also covers cross-origin frames/other targets.
        # Chromium font preferences use whole pixels; retain this rounding in metadata.
        minimum_size = round(wisp_meta["font_min_size_tenths_pt"] * 96 / 720)
        preferences = Path(profile) / "Default/Preferences"
        preferences.parent.mkdir()
        write_json(preferences, {"profile": {"default_content_setting_values": {"javascript": 2}},
            "webkit": {"webprefs": {"minimum_font_size": minimum_size}}})
        process = subprocess.Popen([args.chromium, "--headless", "--remote-debugging-port=0",
            "--remote-debugging-address=127.0.0.1", f"--user-data-dir={profile}",
            "--no-first-run", "--no-default-browser-check", "--disable-background-networking",
            "--disable-extensions", "--disable-dev-shm-usage", "--force-device-scale-factor=1",
            "--hide-scrollbars", "about:blank"], stdout=log, stderr=log)
        try:
            port_file = Path(profile) / "DevToolsActivePort"
            while not port_file.exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Chromium failed to start; see reference/browser.log")
                time.sleep(0.05)
            port = int(port_file.read_text().splitlines()[0])
            # Bypass proxy environment variables for this private localhost endpoint.
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with opener.open(f"http://127.0.0.1:{port}/json/list", timeout=5) as response:
                targets = json.load(response)
            target = next(t for t in targets if t["type"] == "page")
            cdp = DevTools(target["webSocketDebuggerUrl"], deadline)
            version = cdp.call("Browser.getVersion")
            cdp.call("Page.enable")
            cdp.call("Page.setLifecycleEventsEnabled", enabled=True)
            cdp.call("Network.enable")
            cdp.call("Network.setCacheDisabled", cacheDisabled=True)
            cdp.call("Emulation.setScriptExecutionDisabled", value=True)
            width, height = args.size
            cdp.call("Emulation.setDeviceMetricsOverride", width=width, height=height,
                     deviceScaleFactor=1, mobile=False)
            fonts = wisp_meta["fonts"]
            cdp.call("Page.setFontFamilies", fontFamilies={"standard": fonts["sans"]["resolved"],
                "sansSerif": fonts["sans"]["resolved"], "serif": fonts["serif"]["resolved"],
                "fixed": fonts["mono"]["resolved"], "cursive": fonts["cursive"]["resolved"],
                "fantasy": fonts["fantasy"]["resolved"]})
            # CDP accepts only whole-pixel default sizes. Record the rounding.
            default_size = round(wisp_meta["font_size_tenths_pt"] * 96 / 720)
            cdp.call("Page.setFontSizes", fontSizes={"standard": default_size})
            cdp.call("Animation.enable")
            cdp.call("Animation.setPlaybackRate", playbackRate=0)
            navigation = cdp.call("Page.navigate", url=url)
            if navigation.get("errorText") or "loaderId" not in navigation:
                raise RuntimeError(f"Chromium navigation failed: {navigation}")
            cdp.wait_loaded(navigation["loaderId"], args.settle_ms)
            metrics = cdp.call("Page.getLayoutMetrics")
            viewport = metrics["cssLayoutViewport"]
            if (viewport["clientWidth"], viewport["clientHeight"]) != args.size:
                raise RuntimeError(f"Chromium content viewport mismatch: {viewport}")
            picture = cdp.call("Page.captureScreenshot", format="png", fromSurface=True,
                captureBeyondViewport=False, clip={"x": 0, "y": 0, "width": width, "height": height, "scale": 1})
            (directory / "page.png").write_bytes(base64.b64decode(picture["data"]))
            if args.id or args.point:
                write_json(directory / "inspection.json", inspect_chromium(cdp, args.id, args.point))
            frame = cdp.call("Page.getFrameTree")["frameTree"]["frame"]
            metadata = {"result": "complete", "browser": version["product"], "requested_url": url,
                "content_url": frame["url"], "javascript": False, "device_pixel_ratio": 1, "zoom": 1,
                "viewport": {"width": width, "height": height}, "default_font_size_px": default_size,
                "minimum_font_size_px": minimum_size,
                "font_families": fonts, "resource_failures": cdp.failures, "settle_ms": args.settle_ms,
                "css_animations_paused": True, "image_animation": "Chromium default"}
            write_json(directory / "capture.json", metadata)
            return metadata
        finally:
            if cdp:
                cdp.ws.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def compare_images(magick, actual, reference, output):
    if png_size(actual) != png_size(reference):
        raise RuntimeError("Image dimensions differ; refusing to resize and conceal a viewport mismatch")
    def run(*options):
        return subprocess.run([magick, *map(str, options)], check=True, capture_output=True,
                              text=True, timeout=30).stdout
    run(actual, reference, "-alpha", "off", "-compose", "difference", "-composite", output / "difference.png")
    run(actual, reference, "-alpha", "off", "-evaluate-sequence", "mean", output / "overlay.png")
    # Any nonzero RGB channel counts. This is a triage metric, not a pass/fail score.
    fraction = run(output / "difference.png", "-channel", "RGB", "-separate", "-evaluate-sequence", "max",
                   "-threshold", "0", "-format", "%[fx:mean]", "info:")
    return float(fraction)


def dimensions(value):
    try:
        size = tuple(map(int, value.split("x")))
        if len(size) == 2 and all(0 < n <= 8192 for n in size):
            return size
    except ValueError:
        pass
    raise argparse.ArgumentTypeError("size must be WIDTHxHEIGHT, each between 1 and 8192")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="URL or local HTML file (prefer saved fixtures)")
    parser.add_argument("--wisp", required=True, type=Path, help="capture-enabled wisp-qt executable")
    parser.add_argument("--output", required=True, type=Path, help="new or empty output directory")
    parser.add_argument("--size", type=dimensions, default=(1280, 900))
    parser.add_argument("--id", action="append", default=[], help="element ID to inspect in both browsers")
    parser.add_argument("--point", help="X,Y in document pixels; within the viewport for Chromium")
    parser.add_argument("--timeout", type=int, default=30, help="seconds per browser")
    parser.add_argument("--settle-ms", type=int, default=500)
    parser.add_argument("--chromium", default=shutil.which("chromium") or shutil.which("google-chrome"))
    parser.add_argument("--magick", default=shutil.which("magick"))
    parser.add_argument("--reference", type=Path, help="reuse page.png/capture.json from an earlier capture")
    args = parser.parse_args()
    if not args.magick or (not args.reference and not args.chromium):
        parser.error("ImageMagick 7 and (unless --reference is used) Chromium are required")
    if not 1 <= args.timeout <= 600 or not 50 <= args.settle_ms <= 600000:
        parser.error("timeout must be 1..600 seconds; settle-ms must be 50..600000")
    if args.point:
        try:
            point = tuple(map(int, args.point.split(",")))
            assert len(point) == 2 and all(n >= 0 for n in point)
        except (ValueError, AssertionError):
            parser.error("point must be nonnegative X,Y")
    if not args.reference:
        try:
            import websocket
        except ImportError:
            parser.error("Chromium capture requires Python websocket-client (pip install websocket-client)")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    url = args.url if urllib.parse.urlsplit(args.url).scheme in ("http", "https", "file") else Path(args.url).resolve().as_uri()
    try:
        command = [str(args.wisp.resolve()), "--capture-dir", str(output / "wisp"),
            "--capture-size", f"{args.size[0]}x{args.size[1]}", "--capture-timeout", str(args.timeout * 1000),
            "--capture-settle", str(args.settle_ms)]
        for key in args.id:
            command.extend(["--capture-id", key])
        if args.point:
            command.extend(["--capture-point", args.point])
        command.append(url)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
                   QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
        with (output / "wisp.log").open("w") as log:
            subprocess.run(command, env=env, stdout=log, stderr=log, check=True, timeout=args.timeout + 5)
        actual = json.loads((output / "wisp/capture.json").read_text())
        reference_dir = output / "reference"
        if args.reference:
            reference = json.loads((args.reference / "capture.json").read_text())
            reference_dir.mkdir()
            for name in ("capture.json", "page.png", "inspection.json"):
                if (args.reference / name).is_file():
                    shutil.copyfile(args.reference / name, reference_dir / name)
        else:
            reference = chromium_capture(args, url, reference_dir, actual)
        for meta in (actual, reference):
            if (meta["result"] != "complete" or meta["viewport"] != actual["viewport"] or
                    meta["javascript"] or meta["device_pixel_ratio"] != 1 or meta["zoom"] != 1):
                raise RuntimeError("Both captures must be complete, JS off, DPR/zoom 1, and have matching viewports")
        fraction = compare_images(args.magick, output / "wisp/page.png", reference_dir / "page.png", output)
        summary = {"result": "complete", "url": url, "different_pixel_fraction": fraction,
            "reference_source": str(args.reference.resolve()) if args.reference else "Chromium",
            "note": "Pixel differences locate discrepancies; they are not a correctness score.",
            "wisp": actual, "reference": reference}
        write_json(output / "comparison.json", summary)
        title = html.escape(url)
        (output / "report.html").write_text(f'''<!doctype html><meta charset="utf-8">
<title>Wisp rendering comparison</title><style>
body{{font:16px sans-serif;margin:24px;background:#eee;color:#222}}
section{{display:grid;grid-template-columns:1fr 1fr;gap:20px}}figure{{margin:0;min-width:0}}
img{{width:100%;height:auto;background:white}}figcaption{{padding:10px 0;font-weight:bold}}
</style><h1>Rendering comparison</h1><p>{title}</p>
<p>{fraction:.1%} of pixels differ. Font rasterization and live content can contribute.
See <a href="comparison.json">metadata</a> and the inspection JSON in each capture directory.</p>
<section><figure><figcaption>Wisp</figcaption><a href="wisp/page.png"><img src="wisp/page.png"></a></figure>
<figure><figcaption>Reference</figcaption><a href="reference/page.png"><img src="reference/page.png"></a></figure>
<figure><figcaption>50% overlay</figcaption><a href="overlay.png"><img src="overlay.png"></a></figure>
<figure><figcaption>Absolute RGB difference (black = identical)</figcaption>
<a href="difference.png"><img src="difference.png"></a></figure></section>''', encoding="utf-8")
        print(f"Comparison saved: {output / 'report.html'}")
        return 0
    except Exception as error:
        # CLI boundary: also record DevTools/WebSocket failures, preserving logs.
        write_json(output / "comparison.json", {"result": "error", "error": str(error)})
        print(f"Comparison failed: {error}; diagnostics: {output}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
