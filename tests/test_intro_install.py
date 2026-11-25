import hashlib
from pathlib import Path
import tempfile
import unittest
from tools.apply_intro import replace_checked

class IntroReplacementTests(unittest.TestCase):
    def test_atomic_replace_and_restore_preserve_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);target=root/'cache.psarc';candidate=root/'candidate.psarc';original=root/'original.psarc'
            old=b'original archive\x00'*100;new=b'modified archive\xff'*100
            target.write_bytes(old);original.write_bytes(old);candidate.write_bytes(new)
            replace_checked(candidate,target,hashlib.sha256(old).hexdigest())
            self.assertEqual(target.read_bytes(),new)
            replace_checked(original,target,hashlib.sha256(new).hexdigest())
            self.assertEqual(target.read_bytes(),old)
            self.assertEqual(candidate.read_bytes(),new)
            self.assertEqual(original.read_bytes(),old)
            self.assertFalse((root/'cache.psarc.rsmodernized.tmp').exists())

    def test_modified_destination_or_existing_temp_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);target=root/'cache.psarc';candidate=root/'candidate.psarc'
            target.write_bytes(b'changed');candidate.write_bytes(b'new')
            with self.assertRaises(RuntimeError):replace_checked(candidate,target,hashlib.sha256(b'old').hexdigest())
            self.assertEqual(target.read_bytes(),b'changed')
            temp=root/'cache.psarc.rsmodernized.tmp';temp.write_bytes(b'unrelated')
            with self.assertRaises(RuntimeError):replace_checked(candidate,target,hashlib.sha256(b'changed').hexdigest())
            self.assertEqual(temp.read_bytes(),b'unrelated')
            self.assertEqual(target.read_bytes(),b'changed')

if __name__=='__main__':unittest.main()
