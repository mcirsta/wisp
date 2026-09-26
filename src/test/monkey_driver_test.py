"""Regression checks for Monkey's command stream and test-runner lifecycle."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from monkeyfarmer import MonkeyFarmer
import monkey_driver

MONKEY = str(Path(sys.argv.pop(1)).resolve())


class MonkeyDriverTest(unittest.TestCase):
    def test_eof_and_unterminated_command(self):
        with tempfile.TemporaryDirectory(prefix='wisp-monkey-eof-') as profile:
            command = [MONKEY] + [f'--{option}={profile}/{filename}' for option, filename in (
                ('cookie_file', 'cookies'), ('cookie_jar', 'cookies'),
                ('url_file', 'urls'), ('disc_cache_path', 'cache'))]
            for data in (b'', b'WINDOW NEW', b'WINDOW NEW\r\nQUIT\r\n'):
                with self.subTest(data=data):
                    result = subprocess.run(command, input=data, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
                    self.assertIn(b'GENERIC FINISHED', result.stdout)
                    if data:
                        self.assertIn(b'WINDOW NEW WIN', result.stdout)
                        self.assertIn(b'WINDOW DESTROY WIN', result.stdout)

    def test_final_output_is_drained_before_exit(self):
        lines = []
        farmer = MonkeyFarmer(
            [sys.executable, '-c', 'print("GENERIC FINISHED", flush=True)'],
            None, lines.append, quiet=True, timeout=5)
        try:
            while not farmer.deadmonkey:
                farmer.loop(once=True)
            self.assertEqual([line.strip() for line in lines], ['GENERIC FINISHED', 'GENERIC EXIT 0'])
            with self.assertRaises(RuntimeError):
                farmer.loop(once=True)
        finally:
            farmer.close()

    def test_timeout_reaps_browser_and_removes_profile(self):
        browsers = []
        browser_class = monkey_driver.DriverBrowser
        browser_init = browser_class.__init__

        def record_browser(browser, *args, **kwargs):
            browser_init(browser, *args, **kwargs)
            browsers.append(browser)

        ctx = {'monkey': MONKEY}
        plan = {'title': 'Deliberately wait for an event that cannot occur',
                'group': 'driver', 'timeout': 1,
                'steps': [{'action': 'launch'}, {'action': 'window-new', 'tag': 'win'},
                          {'action': 'block', 'conditions': [{'window': 'win', 'status': 'loading'}]}]}
        with patch.object(browser_class, '__init__', record_browser), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(TimeoutError):
                monkey_driver.run_test_plan(ctx, plan)
        self.assertEqual(len(browsers), 1)
        self.assertIsNotNone(browsers[0].farmer.monkey.poll())
        self.assertFalse(ctx['profile'].exists())
        self.assertNotIn('browser', ctx)


if __name__ == '__main__':
    unittest.main()
