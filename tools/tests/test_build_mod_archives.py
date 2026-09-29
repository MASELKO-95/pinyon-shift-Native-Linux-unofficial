import importlib.util
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "build-mod-archives.py"
SPEC = importlib.util.spec_from_file_location("build_mod_archives", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def manifest_line(archive: Path, guest: str) -> str:
    data = archive.read_bytes()
    at = data.rfind(b"PK\x05\x06")
    _, _, _, _, count, size, offset, _ = MODULE.END.unpack_from(data, at)
    return (f'<Zip version="1" path="{guest}" priority="80" dirstart="{offset}" '
            f'dirsize="{size + 22}" direntries="{count}" />')


class BuildModArchivesTests(unittest.TestCase):
    def make(self, directory: Path):
        game = directory / "game"
        tables = game / "media" / "StringTables"
        tables.mkdir(parents=True)
        archive = tables / "EN.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
            for name, data in (("Activities.str", b"activities" * 50),
                               ("PauseMenu.str", b"pause menu"), ("Sub/Other.str", b"other")):
                info = zipfile.ZipInfo(name)
                info.compress_type = zipfile.ZIP_DEFLATED
                # The title's data-offset field; the builder must rewrite it.
                info.extra = struct.pack("<HHI", MODULE.DATA_OFFSET_EXTRA, 4, 0xDEADBEEF)
                zipped.writestr(info, data)
        lines = [manifest_line(archive, "game:\\media\\stringtables\\en.zip"),
                 '<Zip version="1" path="game:\\media\\ui.zip" priority="95" dirstart="1" '
                 'dirsize="2" direntries="3" />']
        (game / "media" / "zipmanifest.xml").write_bytes(
            b"\xef\xbb\xbf<ZipFiles>\r\n" + "\r\n".join(lines).encode() + b"\r\n</ZipFiles>\r\n")
        state = directory / "state"
        (state / "config").mkdir(parents=True)
        (state / "config" / "pinyon_shift.toml").write_text(
            'enabled_mods = "first,second"\n', encoding="utf-8")
        for mod, text in (("first", b"FIRST"), ("second", b"SECOND")):
            members = state / "mods" / mod / "members" / "media" / "StringTables" / "EN.zip"
            members.mkdir(parents=True)
            (members / "PauseMenu.str").write_bytes(text)
        (state / "mods" / "second" / "members" / "media" / "StringTables" / "EN.zip" / "Sub").mkdir()
        (state / "mods" / "second" / "members" / "media" / "StringTables" / "EN.zip" / "Sub"
         / "Other.str").write_bytes(b"replaced other")
        return game, state

    def test_replaces_members_and_keeps_the_rest(self):
        with tempfile.TemporaryDirectory() as directory:
            game, state = self.make(Path(directory))
            result = MODULE.build(state, game)
            self.assertTrue(result["patched"])
            generated = state / "mods" / MODULE.GENERATED / "game"
            rebuilt = generated / "media" / "StringTables" / "EN.zip"
            with zipfile.ZipFile(rebuilt) as zipped:
                self.assertEqual(b"FIRST", zipped.read("PauseMenu.str"))
                self.assertEqual(b"replaced other", zipped.read("Sub/Other.str"))
                self.assertEqual(b"activities" * 50, zipped.read("Activities.str"))
                self.assertEqual(zipfile.ZIP_DEFLATED, zipped.getinfo("Activities.str").compress_type)
                self.assertEqual(zipfile.ZIP_STORED, zipped.getinfo("PauseMenu.str").compress_type)
            data = rebuilt.read_bytes()
            records, _ = MODULE.read_central(data)
            for raw, info in records:
                local = MODULE.LOCAL.unpack_from(data, info["offset"])
                self.assertEqual(info["method"], local[3])
                body = info["offset"] + MODULE.LOCAL.size + local[9] + local[10]
                name_size = MODULE.CENTRAL.unpack_from(raw)[10]
                key, size, offset = struct.unpack_from("<HHI", raw, MODULE.CENTRAL.size + name_size)
                self.assertEqual((MODULE.DATA_OFFSET_EXTRA, 4, body), (key, size, offset))
            manifest = (generated / "media" / "zipmanifest.xml").read_bytes().decode("utf-8-sig")
            self.assertIn(manifest_line(rebuilt, "game:\\media\\stringtables\\en.zip"), manifest)
            self.assertIn('path="game:\\media\\ui.zip" priority="95" dirstart="1"', manifest)
            self.assertIn(f'enabled_mods = "{MODULE.GENERATED},first,second"',
                          (state / "config" / "pinyon_shift.toml").read_text(encoding="utf-8"))
            # The player's archive is untouched.
            with zipfile.ZipFile(game / "media" / "StringTables" / "EN.zip") as zipped:
                self.assertEqual(b"pause menu", zipped.read("PauseMenu.str"))

    def test_removes_the_generated_mod_without_members(self):
        with tempfile.TemporaryDirectory() as directory:
            game, state = self.make(Path(directory))
            MODULE.build(state, game)
            for mod in ("first", "second"):
                import shutil
                shutil.rmtree(state / "mods" / mod / "members")
            self.assertFalse(MODULE.build(state, game)["patched"])
            self.assertFalse((state / "mods" / MODULE.GENERATED).exists())

    def test_refuses_a_member_the_archive_lacks_and_a_mismatched_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            game, state = self.make(Path(directory))
            extra = state / "mods" / "first" / "members" / "media" / "StringTables" / "EN.zip"
            (extra / "New.str").write_bytes(b"new")
            with self.assertRaises(MODULE.ArchiveError):
                MODULE.build(state, game)
            (extra / "New.str").unlink()
            manifest = game / "media" / "zipmanifest.xml"
            manifest.write_bytes(manifest.read_bytes().replace(b'direntries="3"', b'direntries="9"', 1))
            with self.assertRaises(MODULE.ArchiveError):
                MODULE.build(state, game)


if __name__ == "__main__":
    unittest.main()
