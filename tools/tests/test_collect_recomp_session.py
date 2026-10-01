import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "collector", Path(__file__).parents[1] / "collect-recomp-session.py")
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class SessionCollectionTests(unittest.TestCase):
    def test_reused_pid_does_not_select_old_session(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory)
            logs = state / "logs"
            logs.mkdir()
            output = state / "run"
            for session in ("old-p4804", "new-p4804"):
                events = [dict(session=session, event="fh1.render_test.configured",
                               output=str(output)),
                          dict(session=session, event="fh1.render_test.complete")]
                (logs / f"{session}.jsonl").write_text(
                    "\n".join(json.dumps(e) for e in events))
                (logs / f"{session}.perf.csv").write_text(session)
            collector.collect(state, "new-p4804", output)
            self.assertEqual((output / "perf.csv").read_text(), "new-p4804")
            with self.assertRaisesRegex(ValueError, "output identity"):
                collector.collect(state, "new-p4804", state / "wrong-run")


if __name__ == "__main__":
    unittest.main()
