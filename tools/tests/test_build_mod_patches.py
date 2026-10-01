import importlib.util
import sqlite3
from contextlib import closing
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "build-mod-patches.py"
SPEC = importlib.util.spec_from_file_location("build_mod_patches", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class BuildModPatchesTests(unittest.TestCase):
    def make(self, directory: Path):
        game = directory / "game"
        (game / "media" / "db").mkdir(parents=True)
        with closing(sqlite3.connect(game / "media" / "db" / "gamedb.slt")) as connection:
            connection.execute("create table Data_Car (Id integer, BaseCost integer)")
            connection.execute("insert into Data_Car values (1496, 120000)")
            connection.commit()
        state = directory / "state"
        (state / "config").mkdir(parents=True)
        (state / "config" / "pinyon_shift.toml").write_text(
            'vsync = true\nenabled_mods = "cheap,plain"\n', encoding="utf-8")
        (state / "mods" / "cheap" / "db").mkdir(parents=True)
        (state / "mods" / "cheap" / "db" / "10-price.sql").write_text(
            "update Data_Car set BaseCost = 1000 where Id = 1496;", encoding="utf-8")
        (state / "mods" / "plain").mkdir(parents=True)
        return game, state

    def test_applies_scripts_to_a_copy_and_lists_it_first(self):
        with tempfile.TemporaryDirectory() as directory:
            game, state = self.make(Path(directory))
            result = MODULE.build(state, game)
            self.assertTrue(result["patched"])
            patched = state / "mods" / MODULE.GENERATED / "game" / "media" / "db" / "gamedb.slt"
            with closing(sqlite3.connect(patched)) as connection:
                self.assertEqual(1000, connection.execute(
                    "select BaseCost from Data_Car where Id = 1496").fetchone()[0])
            with closing(sqlite3.connect(game / "media" / "db" / "gamedb.slt")) as connection:
                self.assertEqual(120000, connection.execute(
                    "select BaseCost from Data_Car where Id = 1496").fetchone()[0])
            self.assertIn(f'enabled_mods = "{MODULE.GENERATED},cheap,plain"',
                          (state / "config" / "pinyon_shift.toml").read_text(encoding="utf-8"))

    def test_removes_the_generated_mod_without_scripts(self):
        with tempfile.TemporaryDirectory() as directory:
            game, state = self.make(Path(directory))
            MODULE.build(state, game)
            (state / "mods" / "cheap" / "db" / "10-price.sql").unlink()
            result = MODULE.build(state, game)
            self.assertFalse(result["patched"])
            self.assertFalse((state / "mods" / MODULE.GENERATED).exists())
            self.assertIn('enabled_mods = "cheap,plain"',
                          (state / "config" / "pinyon_shift.toml").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
