import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from render_test_events import find_event_log, load_presenters  # noqa: E402


class RenderTestEventsTests(unittest.TestCase):
    def test_presenters_come_from_capture_events_only(self):
        with tempfile.TemporaryDirectory() as directory:
            logs = Path(directory) / "logs"
            logs.mkdir()
            events = logs / "run.jsonl"
            events.write_text(
                '{"event":"fh1.render_test.capture","name":"race","presenter":"native"}\n'
                '{"event":"fh1.render_test.capture","name":"title","presenter":"xenos"}\n'
                '{"event":"fh1.render_test.capture","name":"legacy"}\n'
                '{"event":"fh1.render_test.wait","frame":"10"}\n'
                "not json\n",
                encoding="utf-8",
            )
            self.assertEqual(find_event_log(Path(directory)), events)
            self.assertEqual(load_presenters(events), {"race": "native", "title": "xenos"})


if __name__ == "__main__":
    unittest.main()
