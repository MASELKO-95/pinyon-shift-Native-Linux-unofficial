import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "fh1-strings.py"
SPEC = importlib.util.spec_from_file_location("fh1_strings", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def table(strings):
    pool = b""
    entries = b""
    for key, text in strings:
        entries += struct.pack(">HI", key, len(pool) // 2)
        pool += text.encode("utf-16-be") + b"\x00\x00"
    entries += struct.pack(">HI", 0xFFFF, len(pool) // 2 - 1)
    chunk = struct.pack(">II", 8 + len(entries) + len(pool), len(strings)) + entries + pool
    header = b"LSB2" + struct.pack("<I", 1) + struct.pack(">IIII", 2 << 24, 24, 1, 24)
    return header + chunk


class Fh1StringsTests(unittest.TestCase):
    def test_lists_keys_and_text(self):
        data = table([(0x1126, "RESUME"), (0xDD6B, "MULTIPLAYER"), (0xDED7, "PHOTO MODE")])
        self.assertEqual([(0x1126, "RESUME"), (0xDD6B, "MULTIPLAYER"), (0xDED7, "PHOTO MODE")],
                         MODULE.parse(data))

    def test_rejects_other_files(self):
        with self.assertRaises(MODULE.StringTableError):
            MODULE.parse(b"PK\x03\x04" + b"\x00" * 40)
        data = bytearray(table([(0x1126, "RESUME")]))
        data[24 + 8 + 6:24 + 8 + 8] = b"\x00\x01"  # the sentinel key
        with self.assertRaises(MODULE.StringTableError):
            MODULE.parse(bytes(data))

    def test_cli_filters_by_text(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PauseMenu.str"
            path.write_bytes(table([(0x1126, "RESUME"), (0xDED7, "PHOTO MODE")]))
            self.assertEqual(0, MODULE.main(["--file", str(path), "--grep", "photo"]))


if __name__ == "__main__":
    unittest.main()
