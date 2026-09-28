"""Read FH1 render-test capture events from a runtime JSON Lines log."""

import json
from pathlib import Path


def load_presenters(events: Path) -> dict[str, str]:
    """Map capture name to the renderer that presented its output frame.

    Values are "xenos", "pilot" (the frozen six-family path) or "native"
    (the native executor). Captures recorded before the presenter field
    existed are omitted, so callers can fall back to image heuristics.
    """
    presenters = {}
    for line in events.read_text(encoding="utf-8", errors="replace").splitlines():
        if '"fh1.render_test.capture"' not in line:
            continue
        try:
            event = json.loads(line[line.index("{"):])
        except ValueError:
            continue
        if event.get("event") == "fh1.render_test.capture" and "presenter" in event:
            presenters[event["name"]] = event["presenter"]
    return presenters


def find_event_log(state_root: Path) -> Path | None:
    """Newest runtime JSON Lines log under a render-test state directory."""
    logs = sorted((state_root / "logs").glob("*.jsonl"), key=lambda path: path.stat().st_mtime)
    return logs[-1] if logs else None
