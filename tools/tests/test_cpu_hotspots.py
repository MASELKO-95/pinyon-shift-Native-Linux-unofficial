import importlib.util
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).parents[1] / "summarize-cpu-hotspots.py"
SPEC = importlib.util.spec_from_file_location("cpu_hotspots", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CpuHotspotTests(unittest.TestCase):
    def test_attributes_samples_and_waits_to_preceding_source_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            markers = root / "markers.csv"
            samples = root / "samples.csv"
            waits = root / "waits.csv"
            markers.write_text("timestamp_ms,source_frame\n10,7\n20,8\n25,9\n30,10\n", encoding="utf-8")
            samples.write_text(
                "timestamp_ms,cpu_ms,module,function\n9,1,old,before\n11,1.5,title,guest\n21,2,gpu,submit\n",
                encoding="utf-8",
            )
            waits.write_text(
                "timestamp_ms,wait_ms,wait_reason\n18,4,Event\n22,1,Fence\n",
                encoding="utf-8",
            )

            report = MODULE.summarize(markers, samples, waits)

            self.assertEqual(report["source_frames"], 3)
            self.assertEqual(report["per_frame_ms"]["source_median"], 5.0)
            self.assertEqual(report["unmatched_rows"]["samples"], 1)
            self.assertEqual(report["per_frame_ms"]["cpu_median"], 1.5)
            self.assertEqual(report["top_functions"][0], {"name": "gpu!submit", "ms": 2.0})
            self.assertEqual(report["top_wait_reasons"][0], {"name": "Event", "ms": 4.0})
            self.assertEqual(report["per_frame_ms"]["wait_median"], 2.0)
            self.assertEqual(report["frames"], [
                {"source_frame": 7, "cpu_ms": 1.5, "wait_ms": 2.0, "interval_ms": 10.0},
                {"source_frame": 8, "cpu_ms": 2.0, "wait_ms": 3.0, "interval_ms": 5.0},
                {"source_frame": 9, "cpu_ms": 0.0, "wait_ms": 0.0, "interval_ms": 5.0},
            ])


if __name__ == "__main__":
    unittest.main()
