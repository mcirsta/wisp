#!/usr/bin/env python3
"""Close/navigate real windows and shut down during a yielded restyle."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).resolve().parents[2] / "test/checked-interaction.html"
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
           QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
with tempfile.TemporaryDirectory(prefix="wisp-restyle-lifecycle-") as tmp:
    tmp = Path(tmp)
    page = tmp / "lifecycle.html"
    # Ensure a partial slice yields with both old and newly constructed boxes.
    source = fixture.read_text().replace("</style>", "</style><section>" + "<p>Unchanged text</p>" * 256 + "</section>", 1)
    page.write_text(source)
    for mode in ("close", "navigate", "shutdown", "shutdown-full"):
        out = tmp / mode
        args = [str(binary), "--capture-dir", str(out), "--capture-size", "600x700",
                "--capture-settle", "50", "--capture-timeout", "20000",
                "--capture-interrupt-restyle", mode, "--capture-click-label", "second",
                "--capture-click-label", "first", str(page)]
        result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
        report = out / "interruption.json"
        data = json.loads(report.read_text()) if report.exists() else {}
        assert result.returncode == 0, (mode, data, result.stderr)
        assert "runtime error:" not in result.stderr, (mode, result.stderr)
        assert data["result"] == "complete" and data["closed_during_build"], (mode, data)
        before, after = data["before"], data["after"]
        assert before["building"] and before["conversion"] and before["old_tree"], (mode, data)
        if mode == "shutdown-full":
            assert not before["partial"] and before["styled_elements"] > 0, (mode, data)
        else:
            assert before["partial"] and before["reused_styles"] > 0 and before["table_entries"] > 0, (mode, data)
        if mode.startswith("shutdown"):
            # Exit with an active conversion. Normal core shutdown must cancel
            # it safely rather than relying on another construction slice.
            assert after["building"] and after["conversion"] and not after["window_attached"], (mode, data)
            released = data["released"]
            assert not released["locked"] and not released["building"] and not released["conversion"], (mode, data)
            assert released["error"] and released["table_entries"] == released["active_roots"] == released["pending_roots"] == 0, (mode, data)
            continue
        assert after["settled"] and not after["window_attached"] and not after["locked"], (mode, data)
        assert after["restarts"] == 1 and not after["old_tree"] and not after["conversion"], (mode, data)
        assert after["table_entries"] == after["active_roots"] == after["pending_roots"] == 0, (mode, data)
        if mode == "navigate":
            assert data["current_url"] == "about:blank", data
print("Close, navigation and shutdown safely retire interrupted selective and full presentations.")
