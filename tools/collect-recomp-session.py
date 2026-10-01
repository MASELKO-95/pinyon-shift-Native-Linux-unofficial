"""Archive one completed render-test session by full ID, never by PID alone."""
import argparse
import json
from pathlib import Path
import shutil


def collect(state: Path, session: str, output: Path) -> None:
    if Path(session).name != session or "/" in session or "\\" in session:
        raise ValueError("Session must be a filename stem")
    log = state / "logs" / f"{session}.jsonl"
    events = [json.loads(line) for line in log.read_text().splitlines() if line]
    if not events or any(e.get("session") != session for e in events):
        raise ValueError("Event session identity mismatch")
    configured = [e for e in events if e.get("event") == "fh1.render_test.configured"]
    if len(configured) != 1 or Path(configured[0]["output"]).resolve() != output.resolve():
        raise ValueError("Render-test output identity mismatch")
    if any(e.get("event") == "fh1.render_test.failure" for e in events):
        raise ValueError("Render test reported failure")
    if not any(e.get("event") == "fh1.render_test.complete" for e in events):
        raise ValueError("Render test has not completed")
    csv = log.with_suffix(".perf.csv")
    if not csv.is_file():
        raise ValueError("Session CSV is missing")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(log, output / "events.jsonl")
    shutil.copy2(csv, output / "perf.csv")
    (output / "session.txt").write_text(session + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("session")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    collect(args.state, args.session, args.output)
    print(f"Collected completed session {args.session}")
