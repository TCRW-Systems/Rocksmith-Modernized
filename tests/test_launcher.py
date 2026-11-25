import base64
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

BUILD = Path(__file__).resolve().parents[1] / 'build' / 'startup'


@unittest.skipUnless(os.name == 'nt' and (BUILD / 'launcher-child.exe').exists(), 'Run startup/build.ps1 on Windows first')
class LauncherIntegrationTests(unittest.TestCase):
    def test_steam_command_in_another_library_preserves_arguments_environment_cwd_and_exit(self):
        with tempfile.TemporaryDirectory(prefix='Rocksmith μ ') as folder:
            root = Path(folder)
            game = root / 'Rocksmith2014.exe'
            shutil.copy2(BUILD / 'launcher-child.exe', game)
            output = root / 'child.txt'
            args = [str(output), '', 'a b', 'a"b', 'trailing slash\\', 'μ name']
            environment = dict(os.environ, RSM_FIXTURE_TOKEN='inherited fixture environment')
            start = time.monotonic()
            result = subprocess.run([str(BUILD / 'RocksmithLaunch.exe'), str(game), *args], env=environment, timeout=10)
            self.assertEqual(result.returncode, 37)
            self.assertGreaterEqual(time.monotonic() - start, .3)
            lines = output.read_text(encoding='utf-8-sig').splitlines()
            self.assertEqual(Path(lines[0]), root)
            self.assertEqual(lines[1], environment['RSM_FIXTURE_TOKEN'])
            self.assertEqual([base64.b64decode(line).decode('utf-8') for line in lines[2:]], args)


if __name__ == '__main__':
    unittest.main()
