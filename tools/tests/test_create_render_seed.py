import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "create_render_seed", ROOT / "tools/create-render-seed.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def make_state(root: pathlib.Path) -> pathlib.Path:
    profile = root / "user" / "E00001" / "ForzaProfile" / "ForzaProfile"
    profile.parent.mkdir(parents=True)
    profile.write_bytes(b"save")
    (root / "config").mkdir()
    (root / "config" / "pinyon_shift.toml").write_text("vsync = true\n")
    (root / "cache").mkdir()
    for name in MODULE.CATALOGS:
        (root / "cache" / name).write_bytes(name.encode())
    (root / "cache" / "unrelated.bin").write_bytes(b"x")
    return root


class CreateRenderSeedTests(unittest.TestCase):
    def test_copies_save_and_catalogs_without_touching_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = make_state(pathlib.Path(directory) / "state")
            before = sorted(
                (str(p.relative_to(source)), p.read_bytes())
                for p in source.rglob("*") if p.is_file()
            )
            seed = pathlib.Path(directory) / "seeds" / "recaro"
            manifest = MODULE.create_seed(source, seed, "next: Recaro Rush")

            self.assertEqual(
                (seed / "user/E00001/ForzaProfile/ForzaProfile").read_bytes(),
                b"save",
            )
            self.assertTrue((seed / "config/pinyon_shift.toml").is_file())
            for name in MODULE.CATALOGS:
                self.assertTrue((seed / "cache" / name).is_file())
            self.assertFalse((seed / "cache/unrelated.bin").exists())
            stored = json.loads((seed / "seed.json").read_text(encoding="utf-8"))
            self.assertEqual(stored, manifest)
            self.assertEqual(stored["note"], "next: Recaro Rush")
            self.assertIn("user/E00001/ForzaProfile/ForzaProfile", stored["profiles"])
            after = sorted(
                (str(p.relative_to(source)), p.read_bytes())
                for p in source.rglob("*") if p.is_file()
            )
            self.assertEqual(before, after)

    def test_refuses_to_overwrite_a_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = make_state(pathlib.Path(directory) / "state")
            seed = pathlib.Path(directory) / "seed"
            MODULE.create_seed(source, seed)
            with self.assertRaisesRegex(ValueError, "refusing to overwrite"):
                MODULE.create_seed(source, seed)

    def test_rejects_state_without_profile_or_catalogs(self):
        with tempfile.TemporaryDirectory() as directory:
            source = make_state(pathlib.Path(directory) / "state")
            (source / "cache" / MODULE.CATALOGS[0]).unlink()
            seed = pathlib.Path(directory) / "seed"
            with self.assertRaisesRegex(ValueError, "missing FH1 shader catalogs"):
                MODULE.create_seed(source, seed)
            self.assertFalse(seed.exists())
            empty = pathlib.Path(directory) / "empty"
            empty.mkdir()
            with self.assertRaisesRegex(ValueError, "no FH1 profile"):
                MODULE.create_seed(empty, pathlib.Path(directory) / "seed2")


if __name__ == "__main__":
    unittest.main()
