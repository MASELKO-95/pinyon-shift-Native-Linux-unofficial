import importlib.util
import json
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "summarize-drive-window.py"
SPEC = importlib.util.spec_from_file_location("drive_window", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DriveWindowTest(unittest.TestCase):
    def test_capture_boundaries_select_source_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            perf = root / "perf.csv"
            perf.write_text("source_frame_count,frame_time_us,simulation_time_ns,simulation_tick_count\n"
                            "1,1000,1,1\n2,2000,2,1\n0,3000,3,1\n1,4000,4,1\n1,5000,5,1\n",
                            encoding="utf-8")
            events = root / "events.jsonl"
            events.write_text("\n".join(json.dumps({"event": "fh1.render_test.capture", "name": name,
                                                     "trigger_output_frame": frame,
                                                     "vehicle_pose_valid": "1",
                                                     "vehicle_x": frame, "vehicle_y": 0, "vehicle_z": 0})
                                         for name, frame in (("race-moving", 3), ("race-sustained", 5))),
                              encoding="utf-8")
            result = MODULE.summarize(perf, events, "race-moving", "race-sustained")
            self.assertEqual(result["samples"], 3)
            self.assertEqual(result["median_frame_time_us"], 3000)
            self.assertEqual(result["end_pose"], [5.0, 0.0, 0.0])
            self.assertEqual(result["p99_frame_time_us"], 3000)
            self.assertEqual(result["distance_m"], 2)
            self.assertEqual(result["simulation_ticks"], 3)


if __name__ == "__main__":
    unittest.main()
