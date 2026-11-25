import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools import startup_install as installer
from tools import apply_profile_boot as legacy


class StartupInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'Steam library μ' / 'Rocksmith2014'
        self.game.mkdir(parents=True)
        (self.game / 'Rocksmith2014.exe').write_bytes(b'fixture, never executed')
        self.record = self.root / 'record'
        self.dll, self.ini = self.root / 'proxy.dll', self.root / 'config.ini'
        self.dll.write_bytes(b'new proxy')
        self.ini.write_bytes(b'[Startup]\r\nAutoLoadLastProfile=1\r\n')
        self.old = {'dinput8.dll': b'previous proxy', 'RSStartup.ini': b'previous config'}
        self.closed = patch.object(installer, 'check_closed')
        self.closed.start()
        self.addCleanup(self.closed.stop)

    def prepare(self, update=True):
        if update:
            for name, data in self.old.items():
                (self.game / name).write_bytes(data)
        return installer.prepare(self.game, self.record, self.dll, self.ini, replace_existing=update)

    def fail_second(self, function):
        calls = 0
        def run(*args):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('simulated locked second file')
            return function(*args)
        return run

    def test_interrupted_update_resumes_and_restores_both_originals(self):
        self.prepare()
        with patch.object(installer, 'replace_checked', self.fail_second(installer.replace_checked)):
            with self.assertRaises(OSError):
                installer.transition(self.record)
        self.assertEqual((self.game / 'dinput8.dll').read_bytes(), self.dll.read_bytes())
        self.assertEqual((self.game / 'RSStartup.ini').read_bytes(), self.old['RSStartup.ini'])
        self.assertEqual(installer.transition(self.record)['status'], 'installed_verified')
        installer.transition(self.record, rollback=True)
        self.assertEqual({n: (self.game / n).read_bytes() for n in self.old}, self.old)

    def test_interrupted_rollback_is_retryable(self):
        self.prepare()
        installer.transition(self.record)
        with patch.object(installer, 'replace_checked', self.fail_second(installer.replace_checked)):
            with self.assertRaises(OSError):
                installer.transition(self.record, rollback=True)
        self.assertEqual((self.game / 'dinput8.dll').read_bytes(), self.old['dinput8.dll'])
        self.assertEqual(installer.transition(self.record, rollback=True)['status'], 'rolled_back')
        self.assertEqual(installer.transition(self.record, rollback=True)['status'], 'rolled_back')

    def test_fresh_install_interruption_can_roll_back_without_leaving_files(self):
        self.prepare(update=False)
        with patch.object(installer, 'create_checked', self.fail_second(installer.create_checked)):
            with self.assertRaises(OSError):
                installer.transition(self.record)
        installer.transition(self.record, rollback=True)
        self.assertTrue(all(not (self.game / n).exists() for n in installer.NAMES))
        self.assertTrue((self.game / 'Rocksmith2014.exe').exists())

    def test_unrelated_second_file_edit_prevents_any_rollback(self):
        self.prepare()
        installer.transition(self.record)
        (self.game / 'RSStartup.ini').write_bytes(b'user edited settings')
        with self.assertRaisesRegex(RuntimeError, 'changed outside'):
            installer.transition(self.record, rollback=True)
        self.assertEqual((self.game / 'dinput8.dll').read_bytes(), self.dll.read_bytes())

    def test_corrupt_backup_is_rejected_before_any_rollback(self):
        self.prepare()
        installer.transition(self.record)
        (self.record / 'before' / 'RSStartup.ini').write_bytes(b'corruption')
        with self.assertRaisesRegex(RuntimeError, 'Backup/candidate changed'):
            installer.transition(self.record, rollback=True)
        self.assertEqual((self.game / 'dinput8.dll').read_bytes(), self.dll.read_bytes())

    def test_interruption_after_mutation_before_journal_update_is_recoverable(self):
        self.prepare()
        with patch.object(installer, 'write_json', self.fail_second(installer.write_json)):
            with self.assertRaises(OSError):
                installer.transition(self.record)
        self.assertEqual(installer.transition(self.record)['status'], 'installed_verified')

    def test_original_legacy_partial_rollback_is_recoverable(self):
        value = self.prepare()
        installer.transition(self.record)
        stage = self.root / 'legacy'
        previous = stage / 'previous'
        previous.mkdir(parents=True)
        for name, data in self.old.items():
            (previous / name).write_bytes(data)
        value['status'] = 'installed_verified'
        installer.write_json(stage / 'installation.json', value)
        base = self.root / 'base.json'
        installer.write_json(base, dict(game=str(self.game), files=value['after']))
        with patch.object(legacy, 'check_closed'):
            with patch.object(legacy, 'replace_checked', self.fail_second(legacy.replace_checked)):
                with self.assertRaises(OSError):
                    legacy.rollback(self.game, stage, base)
            self.assertEqual(legacy.rollback(self.game, stage, base)['status'], 'rolled_back')
        self.assertEqual(json.loads(base.read_text())['files'], value['before'])


if __name__ == '__main__':
    unittest.main()
