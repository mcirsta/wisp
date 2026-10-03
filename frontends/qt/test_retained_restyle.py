#!/usr/bin/env python3
"""Retained live updates must match fresh rebuilds, including settled resources."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
profiling = sys.argv[2] == "1"
fixture = Path(__file__).resolve().parents[2] / "test/restyle-tracking.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
ids = ("outside", "notes", "retained-text", "tabs", "first", "second", "first-panel", "second-panel", "after")


def run(out, page, options=(), switches=8, extra_ids=(), actions=None):
    args = [str(binary), "--capture-dir", str(out), "--capture-size", "600x500",
            "--capture-settle", "50", "--capture-timeout", "20000", *options]
    for key in (*ids, *extra_ids):
        args += ["--capture-id", key]
    if actions is None:
        actions = [("click-label", "second" if index % 2 == 0 else "first") for index in range(switches)]
    for kind, value in actions:
        args += ["--capture-" + kind, value]
    result = subprocess.run([*args, str(page)], env=env, capture_output=True, text=True, timeout=30)
    assert "runtime error:" not in result.stderr, result.stderr
    assert (out / "capture.json").exists(), (result.returncode, result.stderr)
    metadata = json.loads((out / "capture.json").read_text())
    steps = json.loads((out / "steps.json").read_text())
    if "--capture-fail-retained-layout" in options:
        assert result.returncode == 1 and metadata["result"] == "content_error", (metadata, result.stderr)
        state = metadata["restyle_state"]
        assert state["error"] and not state["locked"] and not state["building"] and not state["conversion"], state
        assert state["table_entries"] == state["active_roots"] == state["pending_roots"] == 0, state
        assert len(steps) == 3 and not state["window_attached"], metadata
        # The normal error page may already be displayed; the failed tree must
        # never be presented or captured after its layout was interrupted.
        assert not metadata["artifacts"]["page.png"] or metadata["content_url"].startswith("about:query/"), metadata
        return steps
    assert result.returncode == 0, (metadata, result.stderr)
    assert metadata["result"] == "complete" and metadata["load_complete"], metadata
    assert len(steps) == len(actions) + 1, (len(steps), result.stderr)
    return steps


def compare(reference, actual):
    actual = copy.deepcopy(actual)
    reference = copy.deepcopy(reference)
    for series in (reference, actual):
        for step in series:
            for element in step["elements"]:
                for box in element["boxes"]:
                    for key in ("box_address", "owns_region", "box_boundary"):
                        box.pop(key, None)
    for index, (expected, found) in enumerate(zip(reference, actual)):
        for key in ("elements", "scroll", "resource_count", "active_resources", "status"):
            assert expected[key] == found[key], f"state {index}: {key} differs"


with tempfile.TemporaryDirectory(prefix="wisp-retained-") as tmp:
    tmp = Path(tmp)
    page = tmp / "tabs.html"
    gif = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"
    image = f'<img width="8" height="8" src="{gif}">'
    svg = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="12"><rect width="24" height="12" fill="blue"/></svg>'
    source = fixture.read_text().replace("First panel</div>", f"First panel{image}{svg}</div>")
    source = source.replace("Second panel</div>", f"Second panel{image}{svg}</div>")
    source = source.replace("</style>", f'#tabs{{background-image:url("{gif}"),url("{gif}")}}</style>')
    # Native controls are visible and registered; labels still drive the edits.
    source = source.replace("#tabs input { display: none; }", "#tabs input { width:12px;height:12px; }")
    source = source.replace('<section id="tabs">',
                            '<p id="retained-text" style="width:180px">Unchanged words that wrap '
                            '<span>across an inline boundary</span><br>and a hard break.</p><section id="tabs">')
    page.write_text(source)
    full = tmp / "full"
    reference = run(full, page, ("--capture-force-full-restyle",))
    retained = tmp / "retained"
    # No override: ordinary browsing and capture must both retain eligible boxes.
    steps = run(retained, page)
    compare(reference, steps)
    css_only = tmp / "css-only"
    css_steps = run(css_only, page, ("--capture-css-only-restyle",))
    compare(reference, css_steps)
    if profiling:
        for step in css_steps[2:]:
            assert step["restyle_profile"]["update_mode"] == "partial_css", step
            assert step["restyle_profile"]["intrinsic_inline_reuses"] == 0, step
            assert step["restyle_profile"]["inline_layout_reuses"] == 0, step
    assert steps[2]["resource_count"] >= 4, steps[2]
    for index, step in enumerate(steps[3:], 3):
        boxes = {e["id"]: e["boxes"] for e in step["elements"]}
        before = {e["id"]: e["boxes"] for e in steps[2]["elements"]}
        for key in ("outside", "notes", "after"):
            assert boxes[key][0]["box_address"] == before[key][0]["box_address"], (index, key)
        assert step["tree_allocations"] == steps[3 + (index - 3) % 2]["tree_allocations"], index
        assert step["active_resources"] == 0 and step["resource_count"] == steps[2]["resource_count"], index
        if profiling:
            profile = step["restyle_profile"]
            assert profile["update_mode"] == "partial_boxes" and profile["publication_ms"] > 0, profile
            assert profile["reused_styles"] == 0 and profile["construction_wait_ms"] >= 0, profile
            assert 0 <= profile["layout_reset_ms"] <= profile["layout_ms"], profile
            if profile["layout_cpu_ms"] is not None:
                # Allow the final CPU-clock read overhead, not a performance limit.
                assert 0 < profile["layout_cpu_ms"] <= profile["layout_ms"] + 0.5, profile
    for artifact in ("page.png", "dom.txt"):
        assert (retained / artifact).read_bytes() == (full / artifact).read_bytes(), artifact
        assert (css_only / artifact).read_bytes() == (full / artifact).read_bytes(), artifact

    # Intrinsic reuse is separate from both style/box reuse and used layout.
    # Compare every state with fresh builds, not a second pass over the same tree.
    uncached = tmp / "uncached"
    uncached_steps = run(uncached, page, ("--capture-no-intrinsic-reuse",))
    compare(reference, uncached_steps)
    if profiling:
        for index, step in enumerate(steps[3:], 3):
            profile = step["restyle_profile"]
            baseline = uncached_steps[index]["restyle_profile"]
            assert profile["update_mode"] == "partial_boxes", profile
            assert profile["intrinsic_inline_reuses"] > 0, profile
            assert baseline["update_mode"] == "partial_boxes" and baseline["intrinsic_inline_reuses"] == 0, baseline
            assert baseline["inline_layout_reuses"] == 0, baseline
            assert profile["intrinsic_inline_calculations"] < baseline["intrinsic_inline_calculations"], profile
    for artifact in ("page.png", "dom.txt"):
        assert (uncached / artifact).read_bytes() == (full / artifact).read_bytes(), artifact

    intrinsics = tmp / "intrinsics"
    intrinsic_steps = run(intrinsics, page, ("--capture-no-inline-layout-reuse",))
    compare(reference, intrinsic_steps)
    for artifact in ("page.png", "dom.txt"):
        assert (intrinsics / artifact).read_bytes() == (full / artifact).read_bytes(), artifact
    if profiling:
        for index, step in enumerate(steps[3:], 3):
            profile = step["restyle_profile"]
            baseline = intrinsic_steps[index]["restyle_profile"]
            assert profile["inline_layout_reuses"] > 0, profile
            assert baseline["inline_layout_reuses"] == 0 and baseline["intrinsic_inline_reuses"] > 0, baseline
            assert profile["inline_layout_calculations"] < baseline["inline_layout_calculations"], profile

    # A retained paragraph changes width when its flex sibling grows. A float
    # introduced in the dirty scope can instead change wrapping at the same
    # width. Both must reject old lines and agree with fresh construction.
    dependency_source = '''<!doctype html><style>
      body { margin:10px; font:14px/20px sans-serif; }
      .context { width:440px; }
      #tabs input { display:none; } .panel { display:none; }
      #first:checked ~ #first-panel, #second:checked ~ #second-panel { display:block; }
      #first-panel { width:90px; height:40px; background:#acf; }
      #second-panel { width:210px; height:40px; background:#fca; }
      #stable { width:180px; }
      VARIANT
    </style><label for="first">First</label> <label for="second">Second</label>
    <div class="context"><section id="tabs">
      <input id="first" type="radio" name="tabs" checked><input id="second" type="radio" name="tabs">
      <div class="panel" id="first-panel"></div><div class="panel" id="second-panel"></div>
    </section><div id="retained-text">WORDS <span style="padding:0 3px">at an inline boundary</span><br>WORDS</div></div>
    <p id="stable">WORDS</p>'''.replace("WORDS", "Retained text with several words that wrap onto more than one line")
    for name, css, miss in (
            ("width", ".context{display:flex}#tabs{flex:0 0 auto}#retained-text{flex:1;min-width:0}", "width"),
            ("float", "#first-panel{height:0}#second-panel{float:left;height:120px;width:130px}", "float")):
        dependency = tmp / f"{name}.html"
        dependency.write_text(dependency_source.replace("VARIANT", css))
        fresh_dir, reused_dir = tmp / f"{name}-full", tmp / f"{name}-lines"
        fresh = run(fresh_dir, dependency, ("--capture-force-full-restyle",), extra_ids=("stable",))
        reused = run(reused_dir, dependency, extra_ids=("stable",))
        compare(fresh, reused)
        for artifact in ("page.png", "dom.txt"):
            assert (fresh_dir / artifact).read_bytes() == (reused_dir / artifact).read_bytes(), (name, artifact)
        if profiling:
            profiles = [step["restyle_profile"] for step in reused[3:]]
            for index, profile in enumerate(profiles, 3):
                if name == "float" and index % 2 == 0:
                    # Removing an escaping float uses the existing publication
                    # fallback; adding it must instead miss the line cache.
                    assert profile["update_mode"] == "partial_css", profile
                    assert profile["fallback_reason"] == "float_crossing", profile
                    assert profile["inline_layout_reuses"] == 0, profile
                else:
                    assert profile["update_mode"] == "partial_boxes", (name, index, profile)
            assert any(p[f"inline_layout_{miss}_misses"] > 0 for p in profiles), (name, profiles)
            assert any(p["inline_layout_reuses"] > 0 for p in profiles), (name, profiles)

    # Stretch must still resolve percentage descendants even when its text
    # lines can reuse an earlier result from the same layout pass.
    stretch = tmp / "stretch.html"
    stretch.write_text(source.replace('<section id="tabs">', '''
      <div style="display:flex;width:280px;height:140px">
        <div id="stretch-item" style="flex:1;min-width:0">
          <p id="stretch-text" style="margin:0">Several words <span>across an inline boundary</span>
            and enough text to wrap across more than one line.</p>
          <div id="stretch-percent" style="height:50%;background:#acf"></div>
        </div>
      </div>
      <div style="display:flex;flex-direction:column;width:280px">
        <div id="stretch-column"><p id="stretch-column-text" style="margin:0">Short phrase</p></div>
      </div><section id="tabs">'''))
    stretch_ids = ("stretch-item", "stretch-text", "stretch-percent", "stretch-column", "stretch-column-text")
    stretch_full = run(tmp / "stretch-full", stretch,
                       ("--capture-force-full-restyle", "--capture-no-inline-layout-reuse"),
                       extra_ids=stretch_ids)
    stretch_reused = run(tmp / "stretch-reused", stretch, extra_ids=stretch_ids)
    compare(stretch_full, stretch_reused)
    for index, step in enumerate(stretch_reused):
        box = next(e["boxes"][0] for e in step["elements"] if e["id"] == "stretch-percent")
        assert box["content"]["height"] == 70, (index, box)
        if profiling and index > 2:
            profile = step["restyle_profile"]
            assert profile["inline_layout_repeat_reuses"] > 0, (index, profile)
            # The column first measures a narrow item, then stretches its width.
            # That second visit must lay out its lines again.
            assert profile["inline_layout_repeat_changed_width"] > 0, (index, profile)
            assert stretch_full[index]["restyle_profile"]["inline_layout_repeat_reuses"] == 0, index
    for artifact in ("page.png", "dom.txt"):
        assert (tmp / "stretch-full" / artifact).read_bytes() == (tmp / "stretch-reused" / artifact).read_bytes(), artifact

    # Input changes must force a fresh baseline, then permit reuse again.
    # Include viewport units, preserved whitespace and decorated inline edges;
    # no JS or private invalidation hooks drive this resize sequence.
    resized = tmp / "resized.html"
    resized.write_text(source.replace('style="width:180px"', 'style="width:60%;text-indent:3%"')
                       .replace('<span>across', '<span style="padding:1px 3px;margin:0 2px">across')
                       .replace('<section id="tabs">',
                                '<div id="viewport-text" style="font-size:2vw">Words sized to the viewport</div>'
                                '<pre id="preserved">  kept spaces\nand a second line  </pre>'
                                '<div id="nowrap" style="white-space:nowrap">Words that stay together</div>'
                                '<section id="tabs">'))
    actions = [("click-label", "second"), ("click-label", "first"), ("click-label", "second")]
    resumed = [3]
    invalidated = []
    for size in ("340x620", "760x500", "600x500"):
        actions.append(("resize", size))
        invalidated.append(len(actions) + 1)
        actions += [("click-label", key) for key in ("first", "second", "first", "second")]
        resumed += [len(actions) - 1, len(actions)]
    # A round trip without restyling also exercises ordinary reflow of cached boxes.
    actions += [("resize", "420x560"), ("resize", "600x500"), ("click-label", "first")]
    resumed.append(len(actions))
    resize_ids = ("viewport-text", "preserved", "nowrap")
    resize_full = run(tmp / "resize-full", resized, ("--capture-force-full-restyle",),
                      extra_ids=resize_ids, actions=actions)
    resize_reused = run(tmp / "resize-reused", resized, extra_ids=resize_ids, actions=actions)
    compare(resize_full, resize_reused)
    resize_intrinsics = run(tmp / "resize-intrinsics", resized, ("--capture-no-inline-layout-reuse",),
                            extra_ids=resize_ids, actions=actions)
    compare(resize_full, resize_intrinsics)
    for artifact in ("page.png", "dom.txt"):
        assert (tmp / "resize-full" / artifact).read_bytes() == (tmp / "resize-reused" / artifact).read_bytes(), artifact
        assert (tmp / "resize-full" / artifact).read_bytes() == (tmp / "resize-intrinsics" / artifact).read_bytes(), artifact
    if profiling:
        for index in invalidated:
            for series in (resize_reused, resize_intrinsics):
                profile = series[index]["restyle_profile"]
                assert profile["update_mode"] == "full", (index, profile)
                assert profile["fallback_reason"] == "inputs_changed", (index, profile)
                assert profile["intrinsic_inline_reuses"] == profile["inline_layout_reuses"] == 0, (index, profile)
        for index in resumed:
            profile = resize_reused[index]["restyle_profile"]
            assert profile["update_mode"] == "partial_boxes" and profile["intrinsic_inline_reuses"] > 0, (index, profile)
            assert profile["inline_layout_reuses"] > 0, (index, profile)
            assert resize_intrinsics[index]["restyle_profile"]["inline_layout_reuses"] == 0, index

    # The retained ancestor gains/loses a scrollbar as its tab contents grow.
    scrolling = tmp / "scrolling.html"
    scrolling.write_text(source.replace('<section id="tabs">',
                         '<div id="viewport" style="height:160px;overflow:auto"><section id="tabs">')
                         .replace('</section>', '</section></div>')
                         .replace('#second-panel { height: 65px;', '#second-panel { height: 240px;'))
    scrolling_full = run(tmp / "scrolling-full", scrolling, ("--capture-force-full-restyle",), 6, ("viewport",))
    scrolling_steps = run(tmp / "scrolling-retained", scrolling, switches=6, extra_ids=("viewport",))
    compare(scrolling_full, scrolling_steps)
    scrolling_intrinsics = run(tmp / "scrolling-intrinsics", scrolling,
                               ("--capture-no-inline-layout-reuse",), 6, ("viewport",))
    compare(scrolling_full, scrolling_intrinsics)
    viewport = lambda step: next(e["boxes"][0] for e in step["elements"] if e["id"] == "viewport")
    for index, step in enumerate(scrolling_steps[3:], 3):
        assert viewport(step)["box_address"] == viewport(scrolling_steps[2])["box_address"], index
        assert viewport(step)["scrollbars"]["vertical"] == bool(index % 2), (index, viewport(step))

    # A native widget inside the changing scope requires whole reconstruction,
    # but must preserve the existing CSS reuse when its inputs did not change.
    widget = tmp / "widget.html"
    widget.write_text(source.replace("First panel", 'First panel<textarea>kept value</textarea>'))
    widget_full = run(tmp / "widget-full", widget, ("--capture-force-full-restyle",), 4)
    widget_steps = run(tmp / "widget-retained", widget, switches=4)
    compare(widget_full, widget_steps)
    # Known page-level exclusions keep selective CSS reuse. They must not build
    # a candidate only to discard it and redo full CSS selection on every toggle.
    frame = tmp / "frame.html"
    frame.write_text(source + '<iframe src="about:blank" width="60" height="25"></iframe>')
    frame_full = run(tmp / "frame-full", frame, ("--capture-force-full-restyle",), 4)
    frame_steps = run(tmp / "frame-retained", frame, switches=4)
    compare(frame_full, frame_steps)
    if profiling:
        for step in widget_steps[3:]:
            profile = step["restyle_profile"]
            assert profile["update_mode"] == "partial_css" and profile["reused_styles"] > 0, profile
            assert profile["fallback_reason"] != "none" and profile["restarts"] == 0, profile
            assert 0 <= profile["boxes_ms"] <= profile["total_ms"], profile
        for step in frame_steps[2:]:
            assert step["restyle_profile"]["update_mode"] == "partial_css", step
            assert step["restyle_profile"]["intrinsic_inline_reuses"] == 0, step
            assert step["restyle_profile"]["inline_layout_reuses"] == 0, step
        restarted = run(tmp / "restart", page, ("--capture-restart-partial-restyle",))
        compare(reference, restarted)
        assert restarted[3]["restyle_profile"]["restarts"] == 1, restarted[3]
        assert restarted[3]["restyle_profile"]["fallback_reason"] == "inputs_changed_during_build", restarted[3]
        run(tmp / "failure", page, ("--capture-fail-retained-layout",))
print("Retained regions match full rebuilds; outside boxes, resources and selection offsets remain valid.")
