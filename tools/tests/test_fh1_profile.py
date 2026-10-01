import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "fh1-profile.py"
SPEC = importlib.util.spec_from_file_location("fh1_profile", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
# Dataclasses look their module up while the class is built.
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def field(name, kind, payload):
    encoded = name.encode("ascii")
    return struct.pack(">I", len(encoded)) + encoded + struct.pack(">II", 0x20, 0) + bytes([kind]) + payload


def body():
    main = (field("Credits", 0x03, struct.pack(">I", 167700))
            + field("XP", 0x07, struct.pack(">i", -5))
            + field("Exposure", 0x09, struct.pack(">f", 0.1))
            + field("SatNav", 0x01, b"\x01")
            + field("UpTime", 0x04, struct.pack(">Q", 49217825)))
    fields = (field("Options", 0x0F, struct.pack(">I", 1) + field("Tutorial", 0x00, b"\x00"))
              + field("Main", 0x0F, struct.pack(">I", 5) + main))
    return struct.pack(">I", 2) + fields + b"\x00\x00\x00\x02tail" + b"\xbb" * 8


class Fh1ProfileTests(unittest.TestCase):
    def test_round_trips_and_reads_nested_fields(self):
        data = body()
        profile = MODULE.decode(data)
        self.assertEqual(data, MODULE.encode(profile))
        self.assertEqual(167700, MODULE.find(profile, "Main/Credits").value)
        self.assertEqual(-5, MODULE.find(profile, "Main/XP").value)
        self.assertEqual(16, len(profile.tail))

    def test_set_keeps_the_size_and_rejects_values_that_do_not_fit(self):
        profile = MODULE.decode(body())
        MODULE.set_value(profile, "Main/Credits", "1000000")
        encoded = MODULE.encode(profile)
        self.assertEqual(len(body()), len(encoded))
        self.assertEqual(1000000, MODULE.find(MODULE.decode(encoded), "Main/Credits").value)
        with self.assertRaises(MODULE.ProfileError):
            MODULE.set_value(profile, "Main/Credits", "-1")
        with self.assertRaises(KeyError):
            MODULE.find(profile, "Main/Missing")

    def test_rejects_a_body_that_is_not_a_profile(self):
        with self.assertRaises(MODULE.ProfileError):
            MODULE.decode(struct.pack(">I", 1) + b"\x00\x00\x1f\x4d" + b"\x00" * 16)

    def test_cli_set_writes_a_new_file(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "in.bin"
            target = Path(directory) / "out.bin"
            source.write_bytes(body())
            self.assertEqual(0, MODULE.main(["set", str(source), "Main/Credits", "5", "--output", str(target)]))
            self.assertEqual(5, MODULE.find(MODULE.decode(target.read_bytes()), "Main/Credits").value)
            self.assertEqual(body(), source.read_bytes())


if __name__ == "__main__":
    unittest.main()
