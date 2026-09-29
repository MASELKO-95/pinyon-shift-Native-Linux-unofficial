import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "check-fh1-settings-gate.py"
SPEC = importlib.util.spec_from_file_location("check_fh1_settings_gate", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


def passing_events():
    events = [{"event": "process.start", "utc": "2026-09-29T06:27:56Z"}]
    for _ in range(MODULE.CYCLES + MODULE.NAVIGATIONS):
        events += [{"event": "hostui.open", "screen": "SETTINGS"}, {"event": "hostui.closed"}]
    events += [{"event": "hostui.screen", "screen": "DISPLAY", "depth": "2"}] * MODULE.NAVIGATIONS
    events.append({"event": "hostui.layout", "screen": "SETTINGS", "inside": "1",
                   "content": "1,1,2,2", "safe": "0,0,3,3"})
    events.append({"event": "fh1.render_test.capture", "name": "paused",
                   "vehicle_x": "0", "vehicle_y": "0", "vehicle_z": "0"})
    events.append({"event": "fh1.render_test.capture", "name": "driving",
                   "vehicle_x": "100", "vehicle_y": "0", "vehicle_z": "0"})
    return events


class SettingsGateTests(unittest.TestCase):
    def check(self, events):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory)
            (state / "logs").mkdir()
            (state / "logs" / "run.jsonl").write_text(
                "\n".join(json.dumps(event) for event in events) + "\n", encoding="utf-8")
            return MODULE.check(state)

    def test_passes_a_complete_run(self):
        self.assertEqual([], self.check(passing_events()))

    def test_fails_a_leaked_open_screen(self):
        events = passing_events()
        events.remove({"event": "hostui.closed"})
        self.assertTrue(self.check(events))

    def test_fails_a_layout_outside_the_safe_area(self):
        events = passing_events()
        events.append({"event": "hostui.layout", "screen": "DISPLAY", "inside": "0",
                       "content": "0,0,9,9", "safe": "1,1,3,3"})
        self.assertTrue(self.check(events))

    def test_fails_when_the_car_does_not_move(self):
        events = [event for event in passing_events() if event.get("name") != "driving"]
        events.append({"event": "fh1.render_test.capture", "name": "driving",
                       "vehicle_x": "1", "vehicle_y": "0", "vehicle_z": "0"})
        self.assertTrue(self.check(events))


if __name__ == "__main__":
    unittest.main()
