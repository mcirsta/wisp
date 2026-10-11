"""Wrong expected output must yield a readable failure, without a dump overrun."""
from pathlib import Path
import subprocess
import sys
import tempfile


def check(executable):
    # The first case previously allocated eight output bytes, and the second
    # allocated zero. The third also exercises repeated diagnostic growth.
    cases = [
        ("* { overflow:auto visible }", "| *\n", "|  overflow-y: visible\n"),
        ("* { color:red }", "", "|  color: #ffff0000\n"),
        ('.large { background-image:url("' + "x" * 16384 + '") }', "| .large\n", "x" * 16384),
    ]
    with tempfile.TemporaryDirectory(prefix="wisp-css-dump-") as directory:
        path = Path(directory) / "mismatch.dat"
        for css, expected, marker in cases:
            path.write_text(
                f"#data\n{css}\n#errors\n#expected\n{expected}#reset\n\n"
                # A later passing fixture must not hide the earlier failure.
                "#data\n* {}\n#errors\n#expected\n| *\n#reset\n",
                encoding="utf-8",
            )
            result = subprocess.run([executable, str(path)], capture_output=True, text=True, timeout=30)
            assert result.returncode == 1, (result.returncode, result.stdout[-2000:], result.stderr)
            assert "result doesn't match expected" in result.stdout, result.stdout
            assert marker in result.stdout, result.stdout[-2000:]
            assert "Test 2: PASS" in result.stdout, result.stdout[-2000:]
            assert result.stdout.endswith("FAIL\n"), result.stdout[-2000:]
            assert not result.stderr, result.stderr
    print("PASS: oversized and empty expected results report clean mismatches")


if __name__ == "__main__":
    check(sys.argv[1])
