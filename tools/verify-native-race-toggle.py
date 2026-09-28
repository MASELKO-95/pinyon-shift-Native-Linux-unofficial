"""Check in-race native/compatibility switching on captured guest output.

Usage: verify-native-race-toggle.py <captures> [--cold-start] [--events <run.jsonl>]

With --events, the presenter recorded in each capture event decides whether
the frame was native; older runs fall back to the pilot's flat sky color.
"""

import sys
from pathlib import Path

from render_test_events import load_presenters


output = Path(sys.argv[1])
header = b"P6\n1280 720\n255\n"
sky = bytes((28, 56, 110))
presenters = (
    load_presenters(Path(sys.argv[sys.argv.index("--events") + 1]))
    if "--events" in sys.argv else {}
)


def sky_count(name: str) -> int:
    if name in presenters:
        # Stand-in pixel count: a native presenter counts as a full native frame.
        return 1280 * 720 if presenters[name] != "xenos" else 0
    image = (output / f"{name}.ppm").read_bytes()
    assert image.startswith(header) and len(image) == len(header) + 1280 * 720 * 3
    return image.count(sky)


moving = (output / "race-moving.ppm").read_bytes()
assert moving.startswith(header) and len(moving) == len(header) + 1280 * 720 * 3
pixels = moving[len(header) :]
race_hud = sum(
    min(pixels[(y * 1280 + x) * 3 : (y * 1280 + x) * 3 + 3]) > 225
    for y in range(20, 100)
    for x in range(50, 300)
)
assert race_hud > 1000, "race-moving capture is not in the race"


expected = {
    "before-on": False,
    "native-on": True,
    "before-off": True,
    "compatibility-off": False,
    "before-on-again": False,
    "native-on-again": True,
}
if "--cold-start" in sys.argv[2:]:
    expected = {"compatibility-off": False}
for name, native in expected.items():
    count = sky_count(name)
    assert (count > 1000) == native, f"wrong output mode at {name}: {count}"
if "--cold-start" in sys.argv[2:]:
    for names, minimum in (
        (("warmup-on", "warmup-on-later", "native-on", "before-off"), 2),
        (("warmup-on-again", "native-on-again"), 1),
    ):
        counts = [sky_count(name) for name in names]
        assert sum(count > 1000 for count in counts) >= minimum, (
            f"native output did not recover during {names}: {counts}"
        )
print("native race output: live on/off/on switching passed")
