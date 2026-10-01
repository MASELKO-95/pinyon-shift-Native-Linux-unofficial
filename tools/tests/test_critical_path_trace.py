import importlib.util
import json
import pathlib
import tempfile
import tomllib
import unittest


SCRIPT = pathlib.Path(__file__).parents[1] / "summarize-critical-path-trace.py"
SPEC = importlib.util.spec_from_file_location("critical_path", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def event(name, time_ns, frame, value0=0, value1=0, value2=0):
    return {
        "event": name,
        "time_ns": time_ns,
        "thread": 1,
        "source_frame": frame,
        "value0": value0,
        "value1": value1,
        "value2": value2,
    }


class CriticalPathTraceTest(unittest.TestCase):
    def test_title_hooks_are_read_only_and_at_verified_boundaries(self):
        root = pathlib.Path(__file__).parents[2]
        config = tomllib.loads(
            (root / "config/rexglue/analysis/main-xex.toml").read_text(encoding="utf-8")
        )
        expected = {
            0x8240F4D8: "PinyonShiftObserveTitleDrawEmitterBegin",
            0x82410328: "PinyonShiftObserveTitleDrawPacketPublish",
            0x82410620: "PinyonShiftObserveTitleDrawEmitterEnd",
        }
        hooks = {
            hook["address"]: hook
            for hook in config["midasm_hook"]
            if hook["address"] in expected
        }
        self.assertEqual({address: hook["name"] for address, hook in hooks.items()}, expected)
        self.assertTrue(all("jump_address" not in hook for hook in hooks.values()))
        self.assertTrue(all(not hook.get("after_instruction") for hook in hooks.values()))

    def test_candidate_gates_and_submission_join(self):
        events = [
            event("source_frame", 0, 1),
            event("title_emitter", 1, 1, 3_000_000, 200),
            event("submission_begin", 2, 1, 9),
            event("command_tape", 3, 1, 400_000, 8000, 100),
            event("submission_end", 500_002, 1, 9),
            event("gpu_completion", 1_500_002, 1, 9),
            event("guest_vblank_deadline", 4, 1, 1_200_000, 8_333_333),
            event("guest_vblank_dispatch", 5, 1, 20_000),
            event("source_frame", 20_000_000, 2),
        ]
        result = MODULE.summarize(events)
        self.assertTrue(result["gates"]["title_emitter"]["qualifies"])
        self.assertTrue(result["gates"]["vblank"]["qualifies"])
        self.assertEqual(result["metrics_ns"]["submission_recording_median"], 500_000)
        self.assertEqual(result["metrics_ns"]["gpu_completion_median"], 1_000_000)

    def test_log_reader_rejects_partial_records(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "trace.log"
            path.write_text('CRITICAL_PATH {"event":"present"}\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unexpected critical-path fields"):
                MODULE.read_events(path)

    def test_rotated_logs_are_merged_by_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            newer = root / "trace.log"
            older = root / "trace.1.log"
            newer.write_text("CRITICAL_PATH " + json.dumps(event("present", 20, 2)))
            older.write_text("CRITICAL_PATH " + json.dumps(event("present", 10, 1)))
            events = MODULE.read_event_logs([newer, older])
            self.assertEqual([item["time_ns"] for item in events], [10, 20])


if __name__ == "__main__":
    unittest.main()
