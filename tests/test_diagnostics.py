import json
from pathlib import Path
import struct
import tempfile
import unittest

import rocksmith_diag as diag


def dump_fixture(address=0x10001234, declared_modules=1):
    data = bytearray(1024)
    data[:4] = b"MDMP"
    struct.pack_into("<II", data, 8, 3, 32)
    for i, entry in enumerate([(7, 56, 80), (4, 112, 160), (6, 168, 400)]):
        struct.pack_into("<III", data, 32 + i * 12, *entry)
    struct.pack_into("<H", data, 80, 0)  # x86
    data[86] = 16
    struct.pack_into("<I", data, 160, declared_modules)
    struct.pack_into("<QI", data, 164, 0x10000000, 0x10000)
    struct.pack_into("<I", data, 184, 700)
    name = r"C:\game\test.dll".encode("utf-16-le")
    struct.pack_into("<I", data, 700, len(name))
    data[704:704 + len(name)] = name
    struct.pack_into("<IIQQII", data, 408, 0xC0000005, 0, 0, address, 2, 0)
    struct.pack_into("<QQ", data, 440, 8, address)
    return bytes(data)


class DumpTests(unittest.TestCase):
    def test_fault_location_and_access(self):
        result = diag.parse_minidump(dump_fixture())
        self.assertEqual(result["exception"]["module"], "test.dll")
        self.assertEqual(result["exception"]["module_offset"], "0x1234")
        self.assertEqual(result["exception"]["access"], "execute")

    def test_x86_sign_extension(self):
        result = diag.parse_minidump(dump_fixture(0xFFFFFFFF10001234))
        self.assertEqual(result["exception"]["module"], "test.dll")

    def test_unmapped_address_is_not_assigned_to_a_module(self):
        result = diag.parse_minidump(dump_fixture(0x22222222))
        self.assertIsNone(result["exception"]["module"])

    def test_truncation_and_invalid_counts_are_rejected(self):
        for data in [b"MDMP", dump_fixture()[:500], dump_fixture(declared_modules=5000)]:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                diag.parse_minidump(data)


class CaptureTests(unittest.TestCase):
    def test_output_cannot_be_inside_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "game"
            game.mkdir()
            with self.assertRaises(ValueError):
                diag.ensure_output(game, game / "reports", "baseline")

    def test_capture_preserves_sources_and_survives_bad_dump(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game, output = root / "game", root / "report"
            (game / "dlc" / "artist").mkdir(parents=True)
            output.mkdir()
            (game / "dlc" / "artist" / "song.psarc").write_bytes(b"archive")
            (game / "dlc" / "song.psarc").write_bytes(b"other")
            (game / "bad.mdmp").write_bytes(b"broken")
            (game / "RS_ASIO-log.txt").write_text("Wrapper DLL loaded (v0.7.4)\n0.1 xrun\n")
            before = {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()}
            report = diag.snapshot(game, output)
            after = {p.relative_to(game): p.read_bytes() for p in game.rglob("*") if p.is_file()}
            self.assertEqual(before, after)
            self.assertEqual(report["library"]["archive_count"], 2)
            self.assertEqual(report["library"]["duplicate_filenames"], {"song.psarc": 2})
            self.assertIn("error", report["crashes"][0])
            self.assertEqual(report["versions_from_last_logs"]["RS_ASIO"], "0.7.4")
            self.assertTrue((output / "snapshot.json").is_file())

    def test_marker_requires_session_and_records_time(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = Path(tmp)
            with self.assertRaises(ValueError):
                diag.append_event(session, "ready")
            (session / "session.json").write_text("{}")
            diag.append_event(session, "ready")
            event = json.loads((session / "events.jsonl").read_text())
            self.assertEqual(event["label"], "ready")
            self.assertGreater(event["unix_seconds"], 0)


if __name__ == "__main__":
    unittest.main()
