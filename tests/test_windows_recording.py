"""Exercise Windows process APIs using a sleeping Python process, not the game."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import rocksmith_diag as diag


@unittest.skipUnless(os.name == "nt", "Windows process APIs")
class WindowsRecordingTests(unittest.TestCase):
    def test_capture_and_process_exit_without_launching_game(self):
        # The fixture lives in a verified temporary directory, never the installation.
        with tempfile.TemporaryDirectory(prefix="rsdiag-test-") as tmp:
            root = Path(tmp).resolve()
            game = root / "fixture"
            game.mkdir()
            executable = game / "Rocksmith2014.exe"
            shutil.copyfile(sys.executable, executable)
            python_dll = f"python{sys.version_info.major}{sys.version_info.minor}.dll"
            shutil.copyfile(Path(sys.base_prefix) / python_dll, game / python_dll)
            before = executable.read_bytes()
            environment = dict(os.environ, PYTHONHOME=sys.base_prefix)
            child = subprocess.Popen(
                [str(executable), "-c", "import time; time.sleep(2)"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW, env=environment,
            )
            try:
                args = argparse.Namespace(output=root / "reports", interval=0.1,
                                          wait_seconds=5, duration=None)
                with contextlib.redirect_stdout(io.StringIO()):
                    session = diag.record(args, game)
                metadata = json.loads((session / "session.json").read_text())
                self.assertEqual(metadata["status"], "process_exited")
                self.assertEqual(metadata["pid"], child.pid)
                samples = [json.loads(line) for line in (session / "samples.jsonl").read_text().splitlines()]
                self.assertGreaterEqual(len(samples), 2)
                self.assertGreater(samples[0]["working_set_bytes"], 0)
                self.assertIn("cpu_percent_one_core", samples[-1])
                self.assertTrue((session / "before" / "snapshot.json").is_file())
                self.assertTrue((session / "after" / "snapshot.json").is_file())
                self.assertEqual(executable.read_bytes(), before)
            finally:
                if child.poll() is None:
                    child.terminate()
                child.wait(timeout=10)

    def test_wait_timeout_is_recorded_without_starting_a_process(self):
        with tempfile.TemporaryDirectory(prefix="rsdiag-test-") as tmp:
            root = Path(tmp).resolve()
            game = root / "fixture"
            game.mkdir()
            args = argparse.Namespace(output=root / "reports", interval=0.05,
                                      wait_seconds=0.1, duration=None)
            with contextlib.redirect_stdout(io.StringIO()):
                session = diag.record(args, game)
            metadata = json.loads((session / "session.json").read_text())
            self.assertEqual(metadata["status"], "no_process_found")
            self.assertNotIn("pid", metadata)
            self.assertIn("ended_utc", metadata)


if __name__ == "__main__":
    unittest.main()
