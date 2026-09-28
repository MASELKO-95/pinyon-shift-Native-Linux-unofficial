import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "run-fh1-render-test.py"
SPEC = importlib.util.spec_from_file_location("run_fh1_render_test", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class Fh1RenderTestRunnerTests(unittest.TestCase):
    def test_native_counter_accepts_owned_clear_and_legacy_draws(self):
        for line, family, count in (
            ("FH1 native owned depth clears 8192", "owned depth", 8192),
            ("FH1 V5 native world-lit draws 12", "world-lit", 12),
            ("FH1 V5 native world-lit vertex draws 9", "world-lit", 9),
        ):
            match = MODULE.NATIVE_COUNTER.search(line)
            self.assertIsNotNone(match)
            self.assertEqual(family, match["family"])
            self.assertEqual(count, int(match["count"]))
        self.assertIsNone(MODULE.NATIVE_COUNTER.search("FH1 native owned depth rejected 42"))

    def test_owned_depth_race_preserves_motion_and_timing_requirements(self):
        scenarios = Path(__file__).parents[2] / "config" / "render-tests"
        legacy = MODULE.parse_scenario(scenarios / "fh1-race.fh1test")
        owned = MODULE.parse_scenario(scenarios / "fh1-owned-depth-race.fh1test")
        self.assertEqual({"owned depth"}, owned[2])
        self.assertEqual(legacy[:2], owned[:2])
        self.assertEqual(legacy[3:], owned[3:])
        def actions(path):
            return [line for line in path.read_text().splitlines() if line and not line.startswith("#")]
        self.assertEqual(actions(scenarios / "fh1-race.fh1test"),
                         actions(scenarios / "fh1-owned-depth-race.fh1test"))

    def test_accepts_state_waits_and_rejects_malformed_ones(self):
        header = MODULE.HEADER + "\ninput 0 0000 0 0 0 0 0 0\n"
        tail = "capture 20 shot\nstop 30\n"
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "route.fh1test"
            for wait in ("wait 10 600 vehicle", "wait 10 600 vehicle-moved 30",
                         "wait 10 600 movie pressstart", "wait 10 600 file carselect"):
                script.write_text(header + wait + "\n" + tail, encoding="utf-8")
                self.assertEqual([(20, "shot")], MODULE.parse_scenario(script)[0])
            script.write_text(header + "wait 10 600 vehicle\nwait 10 600 vehicle\n" + tail,
                              encoding="utf-8")
            with self.assertRaises(ValueError):
                MODULE.parse_scenario(script)
            for wait in ("wait 10 600 vehicle-moved", "wait 10 0 vehicle",
                         "wait 10 600 teleport", "wait 10 600 vehicle-moved 0"):
                script.write_text(header + wait + "\n" + tail, encoding="utf-8")
                with self.assertRaises(ValueError):
                    MODULE.parse_scenario(script)

    def test_race_start_wait_route_parses(self):
        scenarios = Path(__file__).parents[2] / "config" / "render-tests"
        captures = MODULE.parse_scenario(scenarios / "fh1-race-start-wait.fh1test")[0]
        self.assertIn((4330, "race-moving"), captures)

    def test_synchronized_routes_parse(self):
        scenarios = Path(__file__).parents[2] / "config" / "render-tests"
        for name in (
            "fh1-race-sync", "fh1-modes-sync", "fh1-opening-sync", "fh1-long-drive", "fh1-buy-car",
            "fh1-rewind-sync",
        ):
            self.assertTrue(MODULE.parse_scenario(scenarios / f"{name}.fh1test")[0])

    def test_resolves_disc_corpus_ucode_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "ucode").mkdir()
            (root / "ucode" / "pixel-i01-test.bin").write_bytes(b"shader")
            self.assertEqual(
                (root / "ucode").resolve(),
                MODULE.resolve_disc_shader_corpus(root),
            )
            with self.assertRaisesRegex(ValueError, "contains no .bin shaders"):
                MODULE.resolve_disc_shader_corpus(root / "missing")

    def test_shader_capture_summary_can_gate_runtime_misses(self):
        events = [{
            "event": "native_renderer.shader_capture.summary",
            "entries": "2",
            "bytes": "1024",
            "duplicate_callbacks": "3",
            "rejected_callbacks": "0",
        }]
        self.assertEqual(2, MODULE.shader_capture_summary(events, False)["entries"])
        with self.assertRaisesRegex(RuntimeError, "2 runtime translation misses"):
            MODULE.shader_capture_summary(events, True)

    def test_requires_explicit_image_baseline_mode(self):
        with self.assertRaisesRegex(ValueError, "--record-baseline"):
            MODULE.require_image_reference({"scene": (1, 2, 0.1)}, None, False)
        MODULE.require_image_reference({"scene": (1, 2, 0.1)}, None, True)
        MODULE.require_image_reference(
            {"scene": (1, 2, 0.1)}, Path("baseline"), False
        )

    def test_prepares_private_state_without_copying_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            (source / "user").mkdir(parents=True)
            (source / "config").mkdir()
            (source / "cache").mkdir()
            (source / "user" / "profile").write_text("save", encoding="utf-8")
            (source / "config" / "settings.toml").write_text(
                "setting = true", encoding="utf-8"
            )
            (source / "cache" / "shader.bin").write_bytes(b"cache")

            destination = root / "run" / "state"
            MODULE.prepare_isolated_state(source, destination)

            self.assertEqual(
                "save",
                (destination / "user" / "profile").read_text(encoding="utf-8"),
            )
            self.assertTrue((destination / "config" / "settings.toml").is_file())
            self.assertFalse((destination / "cache").exists())

    def test_seeds_only_fh1_native_shader_and_pipeline_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            storage = source / "cache"
            storage.mkdir(parents=True)
            (storage / "fh1-native-shaders-v2.bin").write_bytes(b"shaders")
            (storage / "fh1-native-pipelines-v1.bin").write_bytes(b"pipelines")
            (storage / "unrelated.pnsp").write_bytes(b"pack")
            destination = root / "destination"

            self.assertEqual(
                ["fh1-native-shaders-v2.bin", "fh1-native-pipelines-v1.bin"],
                MODULE.seed_fh1_shader_storage(source, destination),
            )
            copied = destination / "cache"
            self.assertEqual(b"shaders", (copied / "fh1-native-shaders-v2.bin").read_bytes())
            self.assertEqual(b"pipelines", (copied / "fh1-native-pipelines-v1.bin").read_bytes())
            self.assertFalse((copied / "unrelated.pnsp").exists())

    def test_seeds_only_fh1_pipeline_prewarm_allowlist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            (source / "cache").mkdir(parents=True)
            (source / "cache/fh1-gpu-prewarm-v3.txt").write_text(
                "pinyon-shift.fh1-gpu-prewarm.v3\n", encoding="utf-8"
            )
            destination = root / "destination"

            self.assertEqual(
                "fh1-gpu-prewarm-v3.txt",
                MODULE.seed_fh1_pipeline_prewarm(source, destination),
            )
            self.assertTrue(
                (destination / "cache/fh1-gpu-prewarm-v3.txt").is_file()
            )

    def test_parses_v5_pass_family_cost(self):
        line = (
            "FH1 V5 pass family FA79E5778D949D02: attachment 4A23C979E555853F, "
            "first family B45B38234A27121C, first draw 39E7407EABA44369, "
            "copy 0000000000000000, samples 12, draws 87-121 (average 103), "
            "total 20280000 ns, average 1690000 ns, maximum 2000000 ns"
        )
        match = MODULE.PASS_FAMILY.search(line)
        self.assertIsNotNone(match)
        self.assertEqual("FA79E5778D949D02", match.group("family"))
        self.assertEqual("1690000", match.group("average_ns"))

    def test_parses_strict_scenario(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.fh1test"
            path.write_text(
                "pinyon-shift-fh1-render-test-v1\n"
                "# clock-hz 30\n"
                "# require-native tone-map\n"
                "# expect-image scene 3 8 0.1\n"
                "# expect-performance 50000 55 27 31\n"
                "# expect-distinct-presentation 55\n"
                "# expect-simulation-time 0.9 1.1 0\n"
                "# expect-capture-mae scene scene 0\n"
                "# expect-race-hud scene\n"
                "# expect-race-hud-any scene later\n"
                "input 0 0000 0 0 0 0 0 0\n"
                "input 10 1000 0 0 0 0 0 0\n"
                "capture 20 scene\n"
                "capture 25 later\n"
                "stop 30\n",
                encoding="utf-8",
            )
            (
                captures, stop, native, images, performance, distinct,
                simulation_time, capture_mae, race_hud, race_hud_any,
            ) = MODULE.parse_scenario(path)
            self.assertEqual([(20, "scene"), (25, "later")], captures)
            self.assertEqual(30, stop)
            self.assertEqual({"tone-map"}, native)
            self.assertEqual({"scene": (3.0, 8.0, 0.1)}, images)
            self.assertEqual((50000.0, 55.0, 27.0, 31.0), performance)
            self.assertEqual(55.0, distinct)
            self.assertEqual((0.9, 1.1, 0), simulation_time)
            self.assertEqual([("scene", "scene", 0.0)], capture_mae)
            self.assertEqual({"scene"}, race_hud)
            self.assertEqual([{"scene", "later"}], race_hud_any)

    def test_recognizes_fh1_race_hud_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "race.ppm"
            width = height = 100
            pixels = bytearray(width * height * 3)

            def fill(x0, x1, y0, y1, color):
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        offset = (y * width + x) * 3
                        pixels[offset : offset + 3] = bytes(color)

            fill(3, 23, 2, 14, (255, 255, 255))
            fill(78, 97, 2, 14, (255, 255, 255))
            fill(78, 97, 24, 40, (255, 20, 120))
            path.write_bytes(b"P6\n100 100\n255\n" + pixels)
            self.assertGreater(
                MODULE.race_hud_summary(path)["standings_pink_fraction"], 0.01
            )

            fill(78, 97, 24, 40, (255, 255, 255))
            path.write_bytes(b"P6\n100 100\n255\n" + pixels)
            self.assertGreater(
                MODULE.race_hud_summary(path)["standings_white_fraction"], 0.01
            )

            path.write_bytes(b"P6\n100 100\n255\n" + bytes(len(pixels)))
            with self.assertRaisesRegex(RuntimeError, "missing FH1 race HUD"):
                MODULE.race_hud_summary(path)

    def test_capture_mae_detects_missing_mode_transition(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            header = b"P6\n1 1\n255\n"
            (output / "before.ppm").write_bytes(header + bytes((10, 20, 30)))
            (output / "after.ppm").write_bytes(header + bytes((40, 50, 60)))
            self.assertEqual(
                30.0, MODULE.compare_capture_mae(output, "before", "after")
            )

    def test_requires_complete_execution_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corpus.json"
            path.write_text(
                json.dumps({
                    "schema": "pinyon-shift.fh1-gpu-corpus.v3",
                    "unique_keys": 3,
                    "unique_passes": 2,
                    "overflow": 0,
                    "collisions": 0,
                    "pass_collisions": 1,
                }),
                encoding="utf-8",
            )
            self.assertEqual(3, MODULE.load_corpus_summary(path)["unique_keys"])
            path.write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "invalid FH1 execution corpus"):
                MODULE.load_corpus_summary(path)

    def test_rejects_nonzero_initial_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.fh1test"
            path.write_text(
                "pinyon-shift-fh1-render-test-v1\n"
                "input 1 0000 0 0 0 0 0 0\n"
                "capture 20 scene\n"
                "stop 30\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "frame 0"):
                MODULE.parse_scenario(path)

    def test_capture_pose_fields_are_numeric(self):
        event = {
            "vehicle_pose_valid": "1",
            "vehicle_x": "1.25",
            "vehicle_y": "-2.5",
            "vehicle_z": "3.75",
        }
        pose = {
            axis: float(event[f"vehicle_{axis}"]) for axis in ("x", "y", "z")
        }
        self.assertEqual(pose, {"x": 1.25, "y": -2.5, "z": 3.75})

    def test_compares_vehicle_pose_by_wall_time_frame(self):
        baseline = [
            {"frame": 60, "vehicle_pose": {"x": 1.0, "y": 2.0, "z": 3.0}}
        ]
        candidate = [
            {"frame": 60, "vehicle_pose": {"x": 1.3, "y": 2.4, "z": 3.0}}
        ]
        comparison = MODULE.compare_vehicle_poses(candidate, baseline, 0.51)
        self.assertEqual(comparison, [{"frame": 60, "distance": 0.5}])
        with self.assertRaisesRegex(RuntimeError, "differs by"):
            MODULE.compare_vehicle_poses(candidate, baseline, 0.49)

if __name__ == "__main__":
    unittest.main()
