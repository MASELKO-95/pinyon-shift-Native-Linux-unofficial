"""Check that live native output stays confined to active race gameplay.

Usage: verify-native-race-mode-boundary.py <captures> [--events <run.jsonl>]

With --events, a capture counts as native when its capture event records a
non-Xenos presenter; older runs fall back to the pilot's flat sky color.
"""

import sys
from pathlib import Path

from render_test_events import load_presenters


HEADER = b"P6\n1280 720\n255\n"
NATIVE_SKY = bytes((28, 56, 110))


def capture_stats(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data.startswith(HEADER), f"unexpected capture format: {path}"
    assert len(data) == len(HEADER) + 1280 * 720 * 3, f"wrong capture size: {path}"
    pixels = data[len(HEADER) :]
    return pixels.count(NATIVE_SKY), sum(pixels) // len(pixels)


if __name__ == "__main__":
    captures = Path(sys.argv[1])
    presenters = (
        load_presenters(Path(sys.argv[sys.argv.index("--events") + 1]))
        if "--events" in sys.argv else {}
    )

    def is_native(name: str, sky_pixels: int) -> bool:
        if name in presenters:
            return presenters[name] != "xenos"
        count, _ = capture_stats(captures / f"{name}.ppm")
        return count >= sky_pixels

    transition = captures / "free-roam-transition.ppm"
    if transition.exists():
        assert not is_native("free-roam-transition", 1000), "native output leaked into loading transition"
    for name in ("race-sustained", "race-sustained-again"):
        path = captures / f"{name}.ppm"
        if name == "race-sustained" or path.exists():
            assert is_native(name, 101), f"native race output missing: {name}"
    for name in ("race-paused", "free-roam-after-retire", "title-settled"):
        assert not is_native(name, 1000), f"native output leaked into {name}"
        if name != "race-paused":
            _, mean = capture_stats(captures / f"{name}.ppm")
            assert mean > 20, f"{name} is still a loading/blank frame (mean {mean})"
    print("native race mode boundary: race active; pause, free roam, and title compatible")
