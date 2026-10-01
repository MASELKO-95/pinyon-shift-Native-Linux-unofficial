import hashlib
import importlib.util
import struct
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "extract_fh1_shader_corpus", ROOT / "tools" / "extract-fh1-shader-corpus.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def container(code: bytes, vertex: bool = True, interpolators: int = 0) -> bytes:
    virtual_size = 64
    physical_size = len(code)
    header = struct.pack(
        ">9I", 0x102A1101 if vertex else 0x102A1100,
        virtual_size, physical_size, 0, 36, 0, 40, 0, 0
    )
    shader = struct.pack(">6I", 0, len(code), 0, 0, 0, interpolators << 5)
    return header + b"\0" * 4 + shader + code


class ExtractFh1ShaderCorpusTests(unittest.TestCase):
    def test_extracts_executable_shaders_through_the_existing_helper(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "media" / "shaders").mkdir(parents=True)
            (root / "default.xex").write_bytes(b"local executable")
            helper = root / "extractor.exe"
            helper.touch()

            def dump_image(arguments, **kwargs):
                self.assertEqual(arguments[:3], [helper, "--xex-image", root / "default.xex"])
                arguments[3].write_bytes(container(b"\x01\x02\x03\x04" * 3))
                return MODULE.subprocess.CompletedProcess(arguments, 0)

            with patch.object(MODULE.subprocess, "run", side_effect=dump_image):
                manifest = MODULE.extract(root, root / "manifest.json", archive_extractor=helper)
            self.assertEqual(manifest["shader_count"], 1)
            self.assertEqual(manifest["entries"][0]["sources"][0]["path"], "default.xex")

    def test_executable_declaration_is_derived_from_terminated_elements(self):
        data = (b"\xff" * 12
                + struct.pack(">HHIBBBB", 0, 0, 0x002C23A5, 0, 0, 0, 0)
                + struct.pack(">HHIBBBB", 0, 8, 0x002C23A5, 0, 5, 0, 0)
                + bytes.fromhex("00ff0000ffffffff00000000"))
        expected = {(((0, 0, 0x002C23A5, 0, 0),
                      (0, 8, 0x002C23A5, 5, 0)), (16,))}
        self.assertEqual(MODULE.extract_executable_declarations(data), expected)
        self.assertEqual(MODULE.extract_executable_declarations(data[:-1]), set())
        malformed = bytearray(data)
        malformed[20] = 1  # Unsupported declaration method.
        self.assertNotEqual(MODULE.extract_executable_declarations(malformed), expected)

    def test_specialization_preserves_other_instructions_and_export_bits(self):
        words = [0xFFFFFFFF] * 12
        result = MODULE.specialize_vertex_shader(
            struct.pack(">12I", *words), (2, (1,), ((2, 7),), ((10, 1 << 27),))
        )
        expected = words.copy()
        expected[3:6] = [0xC8000000, 0, 0x02000000]
        expected[6] = 0xFFFFFFC7
        expected[10] = 0xF7FFFFFF
        self.assertEqual(struct.pack(">12I", *expected), result)

    def test_patches_fh1_vertex_fetches_from_the_asset_declaration(self):
        code = struct.pack(
            ">6I", 0x05F82000, 0x00000E88, 0,
            0x05F81000, 0x00000FC8, 0,
        )
        shader_elements = ((0, 0, 0), (1, 5, 0))
        declaration = (
            ((0, 0, 0x002A23B9, 0, 0), (0, 12, 0x002C23A5, 5, 0)),
            (20,),
        )

        patched = MODULE.patch_vertex_shader(code, shader_elements, declaration)

        self.assertEqual(
            struct.pack(
                ">6I", 0x25F82000, 0x00393E88, 5,
                0x05F81000, 0x40253FC8, 0x305,
            ),
            patched,
        )

    def test_extracts_and_deduplicates_valid_fxobj_containers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shaders = root / "media" / "shaders"
            shaders.mkdir(parents=True)
            vertex = bytes(range(12))
            pixel = bytes(range(12, 24))
            (shaders / "one.fxobj").write_bytes(b"junk" + container(vertex, interpolators=3))
            (shaders / "two.fxobj").write_bytes(
                container(vertex, interpolators=3) + container(pixel, False, 6)
            )
            tracks = root / "media" / "tracks"
            tracks.mkdir()
            archived_pixel = bytes(range(24, 36))
            with ZipFile(tracks / "bin.zip", "w") as archive:
                archive.writestr("shaders/track/three.fxobj", container(archived_pixel, False, 4))
            output = root / "corpus.json"
            binary_dir = root / "ucode"

            manifest = MODULE.extract(root, output, binary_dir)

            self.assertEqual(4, manifest["container_count"])
            self.assertEqual(3, manifest["shader_count"])
            self.assertEqual(["pixel", "pixel", "vertex"],
                             [e["stage"] for e in manifest["entries"]])
            self.assertEqual([[6], [4], [3]],
                             [e["interpolator_counts"] for e in manifest["entries"]])
            records = read_corpus_blob(binary_dir / MODULE.CORPUS_BLOB)
            self.assertEqual([("pixel", 6), ("pixel", 4), ("vertex", 3)],
                             [record[:2] for record in records])
            self.assertEqual([entry["sha256"] for entry in manifest["entries"]],
                             [hashlib.sha256(record[2]).hexdigest().upper()
                              for record in records])
            self.assertIn("media/tracks/bin.zip!/shaders/track/three.fxobj",
                          manifest["entries"][1]["sources"][0]["path"])
            self.assertTrue(output.is_file())


def read_corpus_blob(path: Path) -> list[tuple[str, int, bytes]]:
    data = path.read_bytes()
    magic, version, count = MODULE.CORPUS_HEADER.unpack_from(data)
    assert magic == MODULE.CORPUS_MAGIC and version == 1
    offset = MODULE.CORPUS_HEADER.size
    records = []
    for _ in range(count):
        stage, interpolators, _, size = MODULE.CORPUS_RECORD.unpack_from(data, offset)
        offset += MODULE.CORPUS_RECORD.size
        records.append((("vertex", "pixel")[stage], interpolators, data[offset:offset + size]))
        offset += size
    assert offset == len(data)
    return records


if __name__ == "__main__":
    unittest.main()
